#include "syscall.h"
#include "../../arch/x86_64/cpu/cpuid.h"
#include "../../arch/x86_64/cpu/gdt.h"
#include "../../arch/x86_64/cpu/msr.h"
#include "../../drivers/char/ps2kbd.h"
#include "../../include/kernel.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/mm/pmm.h"
#include "../../kernel/mm/vmm.h"
#include "../../kernel/panic.h"
#include "../../kernel/sched/sched.h"
#include "../../kernel/sched/task.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

#define SYSCALL_STACK_SIZE (16 * 1024)
#define SYSCALL_FMASK     0x700ULL

#define EFAULT  14
#define EBADF   9
#define ENOENT  2
#define E2BIG   7
#define ENOEXEC 8
#define EINVAL  22
#define ENOSYS  38
#define ENOMEM  12
#define ERANGE  34

#define PROT_WRITE     0x2
#define PROT_EXEC      0x4
#define MAP_PRIVATE    0x02
#define MAP_ANONYMOUS  0x20
#define MAP_FAILED_RET (-12L)

#define EXEC_MAX_ARGS 16
#define EXEC_STR_MAX  128

extern void syscall_entry(void);

uint64_t syscall_kernel_stack_top = 0;

static uint8_t syscall_boot_stack[SYSCALL_STACK_SIZE] __attribute__((aligned(16)));

typedef long (*syscall_fn_t)(syscall_frame_t *frame);

typedef struct {
    char argv[EXEC_MAX_ARGS][EXEC_STR_MAX];
    char envp[EXEC_MAX_ARGS][EXEC_STR_MAX];
    uint8_t stack[PAGE_SIZE];
} exec_args_t;

static long sys_unsupported(syscall_frame_t *frame __attribute__((unused))) {
    return -ENOSYS;
}

void syscall_set_kernel_stack(uint64_t stack_top) {
    syscall_kernel_stack_top = stack_top;
}

void syscall_init(void) {
    const cpuid_features_t *f = cpuid_features();
    if (!f->has_syscall) {
        kprintf("[boot] Syscall: SYSCALL unsupported on this CPU\n");
        return;
    }

    syscall_set_kernel_stack(
        (uint64_t)(uintptr_t)syscall_boot_stack + sizeof(syscall_boot_stack));

    uint64_t efer = rdmsr(MSR_EFER) | EFER_SCE;
    wrmsr(MSR_EFER, efer);
    wrmsr(MSR_STAR, ((uint64_t)GDT_KERNEL_CODE << 32));
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    wrmsr(MSR_FMASK, SYSCALL_FMASK);

    kprintf("[boot] Syscall: SYSCALL entry ready\n");
}

bool user_range_ok(const void *user_ptr, size_t len) {
    uintptr_t start = (uintptr_t)user_ptr;
    if (len == 0) return true;
    if (start == 0 || start >= USER_SPACE_TOP) return false;
    if (len > USER_SPACE_TOP - start) return false;

    uintptr_t end = start + len - 1;
    pte_t *pml4 = vmm_current_pml4();
    for (uintptr_t page = ALIGN_DOWN(start, PAGE_SIZE);
         page <= end;
         page += PAGE_SIZE) {
        uint64_t flags = vmm_get_flags(pml4, page);
        if ((flags & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER))
            return false;
        if (page > USER_SPACE_TOP - PAGE_SIZE) break;
    }
    return true;
}

static int user_page_for_copy(uintptr_t user_addr,
                              bool write,
                              uint64_t *phys_out) {
    if (user_addr == 0 || user_addr >= USER_SPACE_TOP || !phys_out)
        return -EFAULT;

    pte_t *pml4 = vmm_current_pml4();
    uint64_t page = ALIGN_DOWN(user_addr, PAGE_SIZE);
    uint64_t flags = vmm_get_flags(pml4, page);
    if ((flags & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER))
        return -EFAULT;

    if (write && !(flags & VMM_WRITE)) {
        if (!(flags & VMM_COW) ||
            !vmm_handle_page_fault(user_addr, VMM_PRESENT | VMM_WRITE | VMM_USER))
            return -EFAULT;
        flags = vmm_get_flags(pml4, page);
        if ((flags & VMM_WRITE) == 0)
            return -EFAULT;
    }

    uint64_t phys = vmm_get_physical(pml4, user_addr);
    if (phys == PMM_ALLOC_FAILED)
        return -EFAULT;

    *phys_out = phys;
    return 0;
}

static int copy_user_bytes(void *dst,
                           const void *src,
                           size_t len,
                           bool to_user) {
    if (len == 0)
        return 0;
    if (!dst || !src)
        return -EFAULT;

    uintptr_t user = (uintptr_t)(to_user ? dst : src);
    if (user == 0 || user >= USER_SPACE_TOP || len > USER_SPACE_TOP - user)
        return -EFAULT;

    size_t done = 0;
    while (done < len) {
        uintptr_t cur = user + done;
        uint64_t phys = 0;
        int rc = user_page_for_copy(cur, to_user, &phys);
        if (rc < 0)
            return rc;

        size_t page_off = cur & (PAGE_SIZE - 1);
        size_t n = MIN(PAGE_SIZE - page_off, len - done);
        void *phys_ptr = (void *)(uintptr_t)phys;
        if (to_user)
            memcpy(phys_ptr, (const uint8_t *)src + done, n);
        else
            memcpy((uint8_t *)dst + done, phys_ptr, n);
        done += n;
    }

    return 0;
}

int copy_from_user(void *dst, const void *user_src, size_t len) {
    return copy_user_bytes(dst, user_src, len, false);
}

int copy_to_user(void *user_dst, const void *src, size_t len) {
    return copy_user_bytes(user_dst, src, len, true);
}

int copy_user_string(char *dst, const char *user_src, size_t dst_size) {
    if (!dst || dst_size == 0) return -EINVAL;

    for (size_t i = 0; i < dst_size; i++) {
        if (copy_from_user(&dst[i], user_src + i, 1) < 0) {
            dst[0] = '\0';
            return -EFAULT;
        }
        if (dst[i] == '\0') return 0;
    }

    dst[dst_size - 1] = '\0';
    return -EINVAL;
}

static void path_pop(char *out, size_t *len) {
    if (!out || !len || *len <= 1)
        return;

    while (*len > 1 && out[*len - 1] != '/')
        (*len)--;
    if (*len > 1)
        (*len)--;
    out[*len] = '\0';
}

static int path_push(char *out, size_t *len, size_t cap,
                     const char *name, size_t name_len) {
    if (!out || !len || !name || name_len == 0)
        return -EINVAL;

    size_t need = *len + (*len > 1 ? 1 : 0) + name_len + 1;
    if (need > cap)
        return -EINVAL;

    if (*len > 1)
        out[(*len)++] = '/';
    memcpy(out + *len, name, name_len);
    *len += name_len;
    out[*len] = '\0';
    return 0;
}

static int normalize_absolute_path(const char *input,
                                   char *out,
                                   size_t out_size) {
    if (!input || !out || out_size < 2 || input[0] != '/')
        return -EINVAL;

    out[0] = '/';
    out[1] = '\0';
    size_t len = 1;
    const char *p = input;

    while (*p) {
        while (*p == '/')
            p++;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t part_len = (size_t)(p - start);
        if (part_len == 0)
            break;
        if (part_len == 1 && start[0] == '.')
            continue;
        if (part_len == 2 && start[0] == '.' && start[1] == '.') {
            path_pop(out, &len);
            continue;
        }
        int rc = path_push(out, &len, out_size, start, part_len);
        if (rc < 0)
            return rc;
    }

    return 0;
}

static int resolve_path(const char *path, char *out, size_t out_size) {
    if (!path || !out || path[0] == '\0')
        return -EINVAL;
    if (path[0] == '/')
        return normalize_absolute_path(path, out, out_size);
    if (!current_task)
        return -EINVAL;

    char combined[VFS_MAX_PATH];
    size_t cwd_len = strlen(current_task->cwd);
    size_t path_len = strlen(path);
    size_t need = cwd_len + (cwd_len > 1 ? 1 : 0) + path_len + 1;
    if (need > sizeof(combined))
        return -EINVAL;

    memcpy(combined, current_task->cwd, cwd_len);
    size_t off = cwd_len;
    if (off > 1)
        combined[off++] = '/';
    memcpy(combined + off, path, path_len + 1);
    return normalize_absolute_path(combined, out, out_size);
}

static long sys_write_impl(int fd, const void *user_buf, size_t count) {
    const uint8_t *src = (const uint8_t *)user_buf;
    uint8_t chunk[128];
    size_t done = 0;

    while (done < count) {
        size_t n = MIN(sizeof(chunk), count - done);
        int rc = copy_from_user(chunk, src + done, n);
        if (rc < 0) return rc;

        if ((fd == 1 || fd == 2) &&
            (!current_task || !current_task->files[fd].valid)) {
            for (size_t i = 0; i < n; i++) kputchar((char)chunk[i]);
        } else {
            if (!current_task) return -EBADF;
            int written = vfs_write_in(current_task->files, fd, chunk, n);
            if (written < 0) return -EBADF;
            if (written == 0) break;
            n = (size_t)written;
        }
        done += n;
    }

    return (long)done;
}

static long sys_read_impl(int fd, void *user_buf, size_t count) {
    if (fd == 0 && (!current_task || !current_task->files[0].valid)) {
        uint8_t *dst = (uint8_t *)user_buf;
        for (size_t i = 0; i < count; i++) {
            char c = kbd_getchar();
            int rc = copy_to_user(dst + i, &c, 1);
            if (rc < 0) return rc;
            if (c == '\n' || c == '\r') return (long)(i + 1);
        }
        return (long)count;
    }

    uint8_t chunk[128];
    size_t done = 0;
    while (done < count) {
        size_t n = MIN(sizeof(chunk), count - done);
        if (!current_task) return -EBADF;
        int read = vfs_read_in(current_task->files, fd, chunk, n);
        if (read < 0) return -EBADF;
        if (read == 0) break;
        int rc = copy_to_user((uint8_t *)user_buf + done, chunk, (size_t)read);
        if (rc < 0) return rc;
        done += (size_t)read;
        if ((size_t)read < n) break;
    }

    return (long)done;
}

static long sys_open_impl(const char *user_path) {
    char raw[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    int rc = copy_user_string(raw, user_path, sizeof(raw));
    if (rc < 0) return rc;
    rc = resolve_path(raw, path, sizeof(path));
    if (rc < 0) return rc;
    if (!current_task) return -EBADF;
    return (long)vfs_open_in(current_task->files, path);
}

static long sys_close_impl(int fd) {
    if (!current_task || fd < 0 || fd >= VFS_MAX_FDS ||
        !current_task->files[fd].valid)
        return -EBADF;
    vfs_close_in(current_task->files, fd);
    return 0;
}

static long sys_readdir_impl(const char *user_path,
                             uint32_t idx,
                             char *user_name,
                             uint32_t *user_type) {
    char raw[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    char name[VFS_NAME_MAX];
    vnode_type_t type = VFS_FILE;

    int rc = copy_user_string(raw, user_path, sizeof(raw));
    if (rc < 0) return rc;
    rc = resolve_path(raw, path, sizeof(path));
    if (rc < 0) return rc;
    if (!user_name || !user_type) return -EFAULT;

    if (vfs_readdir(path, idx, name, &type) != 0)
        return -ENOENT;

    rc = copy_to_user(user_name, name, sizeof(name));
    if (rc < 0) return rc;

    uint32_t type_value = (uint32_t)type;
    rc = copy_to_user(user_type, &type_value, sizeof(type_value));
    if (rc < 0) return rc;

    return 0;
}

static long sys_procinfo_impl(uint32_t index, task_info_t *user_info) {
    if (!user_info)
        return -EFAULT;

    task_info_t info;
    long rc = task_get_info(index, &info);
    if (rc < 0)
        return rc;

    int copy_rc = copy_to_user(user_info, &info, sizeof(info));
    if (copy_rc < 0)
        return copy_rc;
    return 0;
}

static long sys_chdir_impl(const char *user_path) {
    char raw[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];

    int rc = copy_user_string(raw, user_path, sizeof(raw));
    if (rc < 0) return rc;
    rc = resolve_path(raw, path, sizeof(path));
    if (rc < 0) return rc;

    vnode_t *vn = vfs_lookup(path);
    if (!vn || vn->type != VFS_DIR)
        return -ENOENT;
    if (!current_task)
        return -EINVAL;

    strncpy(current_task->cwd, path, sizeof(current_task->cwd) - 1);
    current_task->cwd[sizeof(current_task->cwd) - 1] = '\0';
    return 0;
}

static long sys_getcwd_impl(char *user_buf, size_t size) {
    if (!current_task || !user_buf)
        return -EFAULT;

    size_t len = strlen(current_task->cwd) + 1;
    if (size < len)
        return -ERANGE;

    int rc = copy_to_user(user_buf, current_task->cwd, len);
    if (rc < 0)
        return rc;
    return (long)len;
}

static long sys_creat_impl(const char *user_path) {
    char raw[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];

    int rc = copy_user_string(raw, user_path, sizeof(raw));
    if (rc < 0) return rc;
    rc = resolve_path(raw, path, sizeof(path));
    if (rc < 0) return rc;
    if (!current_task) return -EBADF;

    if (vfs_create_file(path, NULL, 0) != 0)
        return -ENOENT;
    return (long)vfs_open_in(current_task->files, path);
}

static long sys_dup2_impl(int oldfd, int newfd) {
    if (!current_task || oldfd < 0 || oldfd >= VFS_MAX_FDS ||
        newfd < 0 || newfd >= VFS_MAX_FDS ||
        !current_task->files[oldfd].valid)
        return -EBADF;

    if (oldfd == newfd)
        return newfd;

    current_task->files[newfd] = current_task->files[oldfd];
    return newfd;
}

static long sys_getpid_impl(void) {
    return current_task ? (long)current_task->pid : 0;
}

static long sys_sched_yield_impl(void) {
    sched_yield();
    return 0;
}

static long sys_fork_impl(syscall_frame_t *frame) {
    return task_fork_from_frame(frame);
}

static long sys_exit_impl(int code) {
    if (current_task) task_exit(code);
    return 0;
}

static long sys_waitpid_impl(int pid, int *user_status, int options) {
    int status = 0;
    long ret = task_waitpid(pid, user_status ? &status : NULL, options);
    if (ret > 0 && user_status) {
        int rc = copy_to_user(user_status, &status, sizeof(status));
        if (rc < 0) return rc;
    }
    return ret;
}

static long collect_exec_vector(uint64_t user_vec,
                                char storage[EXEC_MAX_ARGS][EXEC_STR_MAX],
                                size_t *out_count) {
    if (!out_count)
        return -EINVAL;
    *out_count = 0;

    if (user_vec == 0)
        return 0;

    for (size_t i = 0; i < EXEC_MAX_ARGS; i++) {
        uint64_t user_str = 0;
        int rc = copy_from_user(&user_str,
                                (const void *)(uintptr_t)(user_vec + i * sizeof(uint64_t)),
                                sizeof(user_str));
        if (rc < 0)
            return rc;
        if (user_str == 0) {
            *out_count = i;
            return 0;
        }

        rc = copy_user_string(storage[i],
                              (const char *)(uintptr_t)user_str,
                              EXEC_STR_MAX);
        if (rc == -EINVAL)
            return -E2BIG;
        if (rc < 0)
            return rc;
    }

    return -E2BIG;
}

static int push_exec_qword(uint8_t stack[PAGE_SIZE],
                           size_t *sp,
                           uint64_t value) {
    if (*sp < sizeof(uint64_t))
        return -E2BIG;
    *sp -= sizeof(uint64_t);
    uint64_t *slot = (uint64_t *)(void *)(stack + *sp);
    *slot = value;
    return 0;
}

static int place_exec_string(uint8_t stack[PAGE_SIZE],
                             size_t *sp,
                             const char *s,
                             uint64_t *user_ptr_out) {
    size_t len = strlen(s) + 1;
    if (len > *sp)
        return -E2BIG;

    *sp -= len;
    memcpy(stack + *sp, s, len);
    *user_ptr_out = (USER_STACK_TOP - PAGE_SIZE) + *sp;
    return 0;
}

static long build_exec_stack(exec_args_t *args,
                             size_t argc,
                             size_t envc,
                             uint64_t *initial_rsp_out) {
    if (!args || !initial_rsp_out || argc > EXEC_MAX_ARGS || envc > EXEC_MAX_ARGS)
        return -EINVAL;

    memset(args->stack, 0, sizeof(args->stack));

    uint64_t argv_ptrs[EXEC_MAX_ARGS];
    uint64_t envp_ptrs[EXEC_MAX_ARGS];
    size_t sp = PAGE_SIZE;

    for (size_t i = 0; i < argc; i++) {
        int rc = place_exec_string(args->stack, &sp, args->argv[i], &argv_ptrs[i]);
        if (rc < 0) return rc;
    }
    for (size_t i = 0; i < envc; i++) {
        int rc = place_exec_string(args->stack, &sp, args->envp[i], &envp_ptrs[i]);
        if (rc < 0) return rc;
    }

    sp &= ~15ULL;
    size_t pointer_bytes = (1 + argc + 1 + envc + 1) * sizeof(uint64_t);
    if (pointer_bytes > sp)
        return -E2BIG;
    if (((sp - pointer_bytes) & 15ULL) != 0) {
        if (sp < sizeof(uint64_t))
            return -E2BIG;
        sp -= sizeof(uint64_t);
    }

    if (push_exec_qword(args->stack, &sp, 0) < 0) return -E2BIG;
    for (size_t i = envc; i > 0; i--) {
        if (push_exec_qword(args->stack, &sp, envp_ptrs[i - 1]) < 0)
            return -E2BIG;
    }

    if (push_exec_qword(args->stack, &sp, 0) < 0) return -E2BIG;
    for (size_t i = argc; i > 0; i--) {
        if (push_exec_qword(args->stack, &sp, argv_ptrs[i - 1]) < 0)
            return -E2BIG;
    }

    if (push_exec_qword(args->stack, &sp, argc) < 0)
        return -E2BIG;

    *initial_rsp_out = (USER_STACK_TOP - PAGE_SIZE) + sp;
    return 0;
}

static long sys_execve_impl(syscall_frame_t *frame,
                            const char *user_path,
                            uint64_t argv,
                            uint64_t envp) {
    if (!current_task || !current_task->user_mode)
        return -EINVAL;

    char raw[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    int rc = copy_user_string(raw, user_path, sizeof(raw));
    if (rc < 0)
        return rc;
    rc = resolve_path(raw, path, sizeof(path));
    if (rc < 0)
        return rc;

    exec_args_t *args = (exec_args_t *)kmalloc(sizeof(exec_args_t));
    if (!args)
        return -ENOMEM;

    size_t argc = 0;
    size_t envc = 0;
    long vrc = collect_exec_vector(argv, args->argv, &argc);
    if (vrc == 0)
        vrc = collect_exec_vector(envp, args->envp, &envc);
    if (vrc < 0) {
        kfree(args);
        return vrc;
    }

    uint64_t initial_rsp = 0;
    vrc = build_exec_stack(args, argc, envc, &initial_rsp);
    if (vrc < 0) {
        kfree(args);
        return vrc;
    }

    void *image = NULL;
    size_t image_size = 0;
    if (vfs_read_all(path, &image, &image_size) != 0) {
        kfree(args);
        return -ENOENT;
    }

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    uint64_t entry = 0;
    uint64_t stack_rsp = 0;
    rc = task_replace_user_elf(current_task, name, image, image_size,
                               args->stack, sizeof(args->stack), initial_rsp,
                               &entry, &stack_rsp);
    kfree(image);
    kfree(args);
    if (rc != 0)
        return -ENOEXEC;

    frame->user_rip = entry;
    frame->user_rsp = stack_rsp;
    return 0;
}

static int map_user_zero_page(task_t *task, uint64_t virt, uint64_t flags) {
    uint64_t phys = pmm_alloc_frame();
    if (phys == PMM_ALLOC_FAILED) return -ENOMEM;
    memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);

    if (vmm_map_page((pte_t *)(uintptr_t)task->cr3, virt, phys, flags) != 0) {
        pmm_free_frame(phys);
        return -ENOMEM;
    }
    return 0;
}

static void unmap_user_page(task_t *task, uint64_t virt) {
    pte_t *pml4 = (pte_t *)(uintptr_t)task->cr3;
    uint64_t flags = vmm_get_flags(pml4, virt);
    if ((flags & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER))
        return;

    uint64_t phys = vmm_get_physical(pml4, virt);
    vmm_unmap_page(pml4, virt);
    if (phys != PMM_ALLOC_FAILED)
        pmm_free_frame(ALIGN_DOWN(phys, PAGE_SIZE));
}

static long sys_brk_impl(uint64_t requested) {
    task_t *task = current_task;
    if (!task || !task->user_mode) return -EINVAL;
    if (requested == 0) return (long)task->brk_current;
    if (requested < task->brk_base || requested >= task->mmap_next)
        return (long)task->brk_current;

    uint64_t old_top = ALIGN_UP(task->brk_current, PAGE_SIZE);
    uint64_t new_top = ALIGN_UP(requested, PAGE_SIZE);

    if (new_top > old_top) {
        for (uint64_t page = old_top; page < new_top; page += PAGE_SIZE) {
            int rc = map_user_zero_page(task, page,
                                        VMM_USER | VMM_WRITE | VMM_NX);
            if (rc < 0) return (long)task->brk_current;
        }
    } else if (new_top < old_top) {
        for (uint64_t page = new_top; page < old_top; page += PAGE_SIZE)
            unmap_user_page(task, page);
    }

    task->brk_current = requested;
    return (long)task->brk_current;
}

static long sys_mmap_impl(uint64_t addr, size_t len, uint64_t prot,
                          uint64_t flags, int fd, uint64_t offset) {
    UNUSED(offset);

    task_t *task = current_task;
    if (!task || !task->user_mode || len == 0) return MAP_FAILED_RET;
    if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE) || fd != -1)
        return MAP_FAILED_RET;

    uint64_t size = ALIGN_UP(len, PAGE_SIZE);
    uint64_t base = addr ? ALIGN_DOWN(addr, PAGE_SIZE) : task->mmap_next;
    if (base == 0 || base >= USER_SPACE_TOP || size > USER_SPACE_TOP - base)
        return MAP_FAILED_RET;

    uint64_t page_flags = VMM_USER;
    if (prot & PROT_WRITE) page_flags |= VMM_WRITE;
    if (!(prot & PROT_EXEC)) page_flags |= VMM_NX;

    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        int rc = map_user_zero_page(task, base + off, page_flags);
        if (rc < 0) {
            for (uint64_t undo = 0; undo < off; undo += PAGE_SIZE)
                unmap_user_page(task, base + undo);
            return MAP_FAILED_RET;
        }
    }

    if (!addr)
        task->mmap_next = ALIGN_UP(base + size, PAGE_SIZE);

    return (long)base;
}

static long sys_munmap_impl(uint64_t addr, size_t len) {
    task_t *task = current_task;
    if (!task || !task->user_mode || len == 0 || (addr & (PAGE_SIZE - 1)))
        return -EINVAL;
    if (!user_range_ok((const void *)(uintptr_t)addr, len))
        return -EINVAL;

    uint64_t size = ALIGN_UP(len, PAGE_SIZE);
    for (uint64_t off = 0; off < size; off += PAGE_SIZE)
        unmap_user_page(task, addr + off);

    return 0;
}

static long sys_read(syscall_frame_t *frame) {
    return sys_read_impl((int)frame->rdi, (void *)(uintptr_t)frame->rsi,
                         (size_t)frame->rdx);
}

static long sys_write(syscall_frame_t *frame) {
    return sys_write_impl((int)frame->rdi,
                          (const void *)(uintptr_t)frame->rsi,
                          (size_t)frame->rdx);
}

static long sys_open(syscall_frame_t *frame) {
    return sys_open_impl((const char *)(uintptr_t)frame->rdi);
}

static long sys_close(syscall_frame_t *frame) {
    return sys_close_impl((int)frame->rdi);
}

static long sys_mmap(syscall_frame_t *frame) {
    return sys_mmap_impl(frame->rdi, (size_t)frame->rsi, frame->rdx,
                         frame->r10, (int)frame->r8, frame->r9);
}

static long sys_munmap(syscall_frame_t *frame) {
    return sys_munmap_impl(frame->rdi, (size_t)frame->rsi);
}

static long sys_brk(syscall_frame_t *frame) {
    return sys_brk_impl(frame->rdi);
}

static long sys_sched_yield(syscall_frame_t *frame __attribute__((unused))) {
    return sys_sched_yield_impl();
}

static long sys_getpid(syscall_frame_t *frame __attribute__((unused))) {
    return sys_getpid_impl();
}

static long sys_fork(syscall_frame_t *frame) {
    return sys_fork_impl(frame);
}

static long sys_exit(syscall_frame_t *frame) {
    return sys_exit_impl((int)frame->rdi);
}

static long sys_waitpid(syscall_frame_t *frame) {
    return sys_waitpid_impl((int)frame->rdi,
                            (int *)(uintptr_t)frame->rsi,
                            (int)frame->rdx);
}

static long sys_execve(syscall_frame_t *frame) {
    return sys_execve_impl(frame,
                           (const char *)(uintptr_t)frame->rdi,
                           frame->rsi,
                           frame->rdx);
}

static long sys_readdir(syscall_frame_t *frame) {
    return sys_readdir_impl((const char *)(uintptr_t)frame->rdi,
                            (uint32_t)frame->rsi,
                            (char *)(uintptr_t)frame->rdx,
                            (uint32_t *)(uintptr_t)frame->r10);
}

static long sys_procinfo(syscall_frame_t *frame) {
    return sys_procinfo_impl((uint32_t)frame->rdi,
                             (task_info_t *)(uintptr_t)frame->rsi);
}

static long sys_chdir(syscall_frame_t *frame) {
    return sys_chdir_impl((const char *)(uintptr_t)frame->rdi);
}

static long sys_getcwd(syscall_frame_t *frame) {
    return sys_getcwd_impl((char *)(uintptr_t)frame->rdi,
                           (size_t)frame->rsi);
}

static long sys_creat(syscall_frame_t *frame) {
    return sys_creat_impl((const char *)(uintptr_t)frame->rdi);
}

static long sys_dup2(syscall_frame_t *frame) {
    return sys_dup2_impl((int)frame->rdi, (int)frame->rsi);
}

static syscall_fn_t syscall_table[] = {
    [SYS_READ]    = sys_read,
    [SYS_WRITE]   = sys_write,
    [SYS_OPEN]    = sys_open,
    [SYS_CLOSE]   = sys_close,
    [SYS_MMAP]    = sys_mmap,
    [SYS_MUNMAP]  = sys_munmap,
    [SYS_BRK]     = sys_brk,
    [SYS_SCHED_YIELD] = sys_sched_yield,
    [SYS_GETPID]  = sys_getpid,
    [SYS_FORK]    = sys_fork,
    [SYS_EXECVE]  = sys_execve,
    [SYS_EXIT]    = sys_exit,
    [SYS_WAITPID] = sys_waitpid,
    [SYS_READDIR] = sys_readdir,
    [SYS_PROCINFO] = sys_procinfo,
    [SYS_CHDIR]   = sys_chdir,
    [SYS_GETCWD]  = sys_getcwd,
    [SYS_CREAT]   = sys_creat,
    [SYS_DUP2]    = sys_dup2,
};

long syscall_dispatch(syscall_frame_t *frame) {
    syscall_fn_t fn = sys_unsupported;
    if (frame->rax < ARRAY_SIZE(syscall_table) && syscall_table[frame->rax])
        fn = syscall_table[frame->rax];

    long ret = fn(frame);
    frame->rax = (uint64_t)ret;
    return ret;
}
