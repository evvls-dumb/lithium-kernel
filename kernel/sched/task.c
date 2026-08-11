#include "task.h"
#include "sched.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/mm/pmm.h"
#include "../../kernel/mm/vmm.h"
#include "../../kernel/proc/elf.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"
#include "../../kernel/panic.h"

task_t  *current_task = NULL;
uint32_t next_pid     = 1;

static task_t *all_tasks = NULL;
static task_t *zombie_tasks = NULL;
static task_t *init_task = NULL;

#define WNOHANG 1
#define ECHILD  10
#define EINVAL  22
#define ENOMEM  12

/*
 * Initial stack frame for a newly created task.
 * context_switch (in context.asm) pops these in order:
 *   r15, r14, r13, r12, rbp, rbx  (pushed by context_switch on first call)
 * and then ret → task_entry_trampoline.
 */
/* Declared in context.asm — used as the return address for new tasks. */
extern void task_trampoline(void);
extern void user_mode_enter(void);
extern void fork_return_to_user(void);

#define USER_MMAP_BASE  0x0000000050000000ULL

static void build_initial_stack(task_t *t, uint64_t ret_target,
                                uint64_t r12, uint64_t r13) {
    uint64_t *stk = (uint64_t *)((uint8_t *)t->stack_base + TASK_STACK_SIZE);

    *--stk = ret_target; /* ret target */
    *--stk = 0;          /* r15        */
    *--stk = 0;          /* r14        */
    *--stk = r13;        /* r13        */
    *--stk = r12;        /* r12        */
    *--stk = 0;          /* rbp        */
    *--stk = 0;          /* rbx        */

    t->rsp = (uint64_t)(uintptr_t)stk;
}

static void task_register(task_t *t) {
    t->parent = current_task;
    if (t->pid == 1)
        init_task = t;
    t->all_next = all_tasks;
    all_tasks = t;
}

static void reparent_children(task_t *parent) {
    if (!parent)
        return;

    task_t *new_parent = NULL;
    if (init_task && init_task != parent && init_task->state != TASK_DEAD)
        new_parent = init_task;

    for (task_t *t = all_tasks; t; t = t->all_next) {
        if (t->parent == parent)
            t->parent = new_parent;
    }

    if (new_parent && new_parent->state == TASK_BLOCKED)
        new_parent->state = TASK_READY;
}

static void task_unregister(task_t *t) {
    task_t **link = &all_tasks;
    while (*link) {
        if (*link == t) {
            *link = t->all_next;
            t->all_next = NULL;
            return;
        }
        link = &(*link)->all_next;
    }
}

static void task_free(task_t *t) {
    if (t->user_mode && t->cr3)
        vmm_destroy_user_address_space((pte_t *)(uintptr_t)t->cr3);
    if (t->stack_base)
        kfree(t->stack_base);
    kprintf("[task] reaped pid %u (%s)\n", t->pid, t->name);
    kfree(t);
}

static int load_user_image(const void *image,
                           size_t image_size,
                           uint64_t *cr3_out,
                           uint64_t *entry_out,
                           uint64_t *stack_top_out,
                           uint64_t *stack_phys_out,
                           uint64_t *brk_base_out) {
    if (!image || image_size == 0 || !cr3_out || !entry_out ||
        !stack_top_out || !stack_phys_out || !brk_base_out)
        return -1;

    pte_t *pml4 = vmm_create_address_space();
    if (!pml4)
        return -1;

    elf_user_image_t loaded;
    if (elf_load_user_image(pml4, image, image_size, &loaded) != 0) {
        vmm_destroy_user_address_space(pml4);
        return -1;
    }

    uint64_t stack_phys = pmm_alloc_frame();
    if (stack_phys == PMM_ALLOC_FAILED) {
        vmm_destroy_user_address_space(pml4);
        return -1;
    }

    memset((void *)(uintptr_t)stack_phys, 0, PAGE_SIZE);

    if (vmm_map_page(pml4, USER_STACK_TOP - PAGE_SIZE, stack_phys,
                     VMM_USER | VMM_WRITE | VMM_NX) != 0) {
        pmm_free_frame(stack_phys);
        vmm_destroy_user_address_space(pml4);
        return -1;
    }

    *cr3_out = (uint64_t)(uintptr_t)pml4;
    *entry_out = loaded.entry;
    *stack_top_out = USER_STACK_TOP;
    *stack_phys_out = stack_phys;
    *brk_base_out = loaded.brk_base;
    return 0;
}

void task_zombify(task_t *t) {
    if (!t || t->zombie_next)
        return;
    reparent_children(t);
    t->zombie_next = zombie_tasks;
    zombie_tasks = t;
    if (t->parent && t->parent->state == TASK_BLOCKED)
        t->parent->state = TASK_READY;
}

static bool task_has_child(task_t *parent, int pid) {
    for (task_t *t = all_tasks; t; t = t->all_next) {
        if (t->parent == parent && (pid == -1 || (int)t->pid == pid))
            return true;
    }
    return false;
}

static task_t *take_zombie(task_t *parent, int pid) {
    task_t **link = &zombie_tasks;
    while (*link) {
        task_t *z = *link;
        if (z->parent == parent && (pid == -1 || (int)z->pid == pid)) {
            *link = z->zombie_next;
            z->zombie_next = NULL;
            task_unregister(z);
            return z;
        }
        link = &z->zombie_next;
    }
    return NULL;
}

void task_reap_detached_zombies(void) {
    task_t **link = &zombie_tasks;
    while (*link) {
        task_t *z = *link;
        if (z == current_task || z->parent) {
            link = &z->zombie_next;
            continue;
        }
        *link = z->zombie_next;
        z->zombie_next = NULL;
        task_unregister(z);
        task_free(z);
    }
}

long task_waitpid(int pid, int *status, int options) {
    if (pid == 0 || pid < -1 || (options & ~WNOHANG))
        return -EINVAL;

    for (;;) {
        task_t *z = take_zombie(current_task, pid);
        if (z) {
            int code = z->exit_code;
            uint32_t child_pid = z->pid;
            if (status)
                *status = (code & 0xFF) << 8;
            task_free(z);
            return (long)child_pid;
        }

        if (!task_has_child(current_task, pid))
            return -ECHILD;
        if (options & WNOHANG)
            return 0;

        current_task->state = TASK_BLOCKED;
        sched_yield();
    }
}

long task_get_info(uint32_t index, task_info_t *out) {
    if (!out)
        return -EINVAL;

    uint32_t i = 0;
    for (task_t *t = all_tasks; t; t = t->all_next, i++) {
        if (i != index)
            continue;

        memset(out, 0, sizeof(*out));
        out->pid = t->pid;
        out->ppid = t->parent ? t->parent->pid : 0;
        out->state = (uint32_t)t->state;
        out->user_mode = t->user_mode ? 1u : 0u;
        out->ticks = t->ticks;
        strncpy(out->name, t->name, sizeof(out->name) - 1);
        return 0;
    }

    return -EINVAL;
}

long task_fork_from_frame(const struct syscall_frame *frame) {
    if (!current_task || !current_task->user_mode || !frame)
        return -EINVAL;

    task_t *child = (task_t *)kzalloc(sizeof(task_t));
    if (!child) {
        kprintf("[fork] task alloc failed\n");
        return -ENOMEM;
    }

    child->stack_base = kmalloc(TASK_STACK_SIZE);
    if (!child->stack_base) {
        kprintf("[fork] stack alloc failed\n");
        kfree(child);
        return -ENOMEM;
    }

    pte_t *child_pml4 =
        vmm_clone_user_address_space((pte_t *)(uintptr_t)current_task->cr3);
    if (!child_pml4) {
        kprintf("[fork] address-space clone failed\n");
        kfree(child->stack_base);
        kfree(child);
        return -ENOMEM;
    }

    uint8_t *stack_top = (uint8_t *)child->stack_base + TASK_STACK_SIZE;
    syscall_frame_t *child_frame =
        (syscall_frame_t *)(void *)(stack_top - 512);
    memcpy(child_frame, frame, sizeof(*child_frame));
    child_frame->rax = 0;

    child->pid            = next_pid++;
    child->state          = TASK_READY;
    child->cr3            = (uint64_t)(uintptr_t)child_pml4;
    child->user_mode      = true;
    child->user_entry     = current_task->user_entry;
    child->user_stack_top = current_task->user_stack_top;
    child->brk_base       = current_task->brk_base;
    child->brk_current    = current_task->brk_current;
    child->mmap_next      = current_task->mmap_next;
    strncpy(child->cwd, current_task->cwd, sizeof(child->cwd) - 1);
    memcpy(child->files, current_task->files, sizeof(child->files));
    strncpy(child->name, current_task->name, TASK_NAME_MAX - 1);

    build_initial_stack(child, (uint64_t)(uintptr_t)fork_return_to_user,
                        (uint64_t)(uintptr_t)child_frame, 0);

    task_register(child);
    sched_add(child);
    return (long)child->pid;
}

task_t *task_create(const char *name, void (*fn)(void *), void *arg) {
    task_t *t = (task_t *)kzalloc(sizeof(task_t));
    if (!t) return NULL;

    t->stack_base = kmalloc(TASK_STACK_SIZE);
    if (!t->stack_base) { kfree(t); return NULL; }

    t->pid   = next_pid++;
    t->state = TASK_READY;
    strncpy(t->cwd, "/", sizeof(t->cwd) - 1);
    strncpy(t->name, name, TASK_NAME_MAX - 1);

    __asm__ volatile ("mov %%cr3, %0" : "=r"(t->cr3));

    /*
     * Initial kernel stack frame — must match context_switch's pop order.
     * context_switch pushes: r15,r14,r13,r12,rbp,rbx  (last = lowest addr)
     * context_switch pops:   rbx,rbp,r12,r13,r14,r15  then ret.
     *
     * We build from HIGH to LOW (each *--stk goes one slot lower):
     *   slot 7 (highest): task_trampoline  ← ret jumps here
     *   slot 6:  r15 = 0
     *   slot 5:  r14 = 0
     *   slot 4:  r13 = arg                 ← task_trampoline reads rdi←r13
     *   slot 3:  r12 = fn                  ← task_trampoline calls r12
     *   slot 2:  rbp = 0
     *   slot 1:  rbx = 0
     *   slot 0 (lowest = rsp): rbx (first pop)
     *
     * After restoring: ret → task_trampoline → mov rdi,r13; call r12
     */
    build_initial_stack(t, (uint64_t)(uintptr_t)task_trampoline,
                        (uint64_t)(uintptr_t)fn,
                        (uint64_t)(uintptr_t)arg);

    task_register(t);
    sched_add(t);
    return t;
}

task_t *task_create_user_elf(const char *name, const void *image, size_t image_size) {
    if (!image || image_size == 0) return NULL;

    task_t *t = (task_t *)kzalloc(sizeof(task_t));
    if (!t) return NULL;

    t->stack_base = kmalloc(TASK_STACK_SIZE);
    if (!t->stack_base) { kfree(t); return NULL; }

    uint64_t cr3, entry, stack_top, stack_phys, brk_base;
    if (load_user_image(image, image_size, &cr3, &entry,
                        &stack_top, &stack_phys, &brk_base) != 0) {
        kfree(t->stack_base);
        kfree(t);
        return NULL;
    }
    UNUSED(stack_phys);

    t->pid            = next_pid++;
    t->state          = TASK_READY;
    t->cr3            = cr3;
    t->user_mode      = true;
    t->user_entry     = entry;
    t->user_stack_top = stack_top;
    t->brk_base       = brk_base;
    t->brk_current    = brk_base;
    t->mmap_next      = USER_MMAP_BASE;
    strncpy(t->cwd, "/", sizeof(t->cwd) - 1);
    strncpy(t->name, name, TASK_NAME_MAX - 1);

    build_initial_stack(t, (uint64_t)(uintptr_t)user_mode_enter,
                        t->user_entry, t->user_stack_top);

    task_register(t);
    sched_add(t);
    return t;
}

int task_replace_user_elf(task_t *t, const char *name,
                          const void *image, size_t image_size,
                          const void *stack_image,
                          size_t stack_image_size,
                          uint64_t initial_rsp,
                          uint64_t *entry_out, uint64_t *stack_top_out) {
    if (!t || !t->user_mode || !image || image_size == 0)
        return -1;

    uint64_t new_cr3, entry, stack_top, stack_phys, brk_base;
    if (load_user_image(image, image_size, &new_cr3, &entry,
                        &stack_top, &stack_phys, &brk_base) != 0)
        return -1;

    if (stack_image) {
        uint64_t stack_base = stack_top - PAGE_SIZE;
        if (stack_image_size > PAGE_SIZE ||
            initial_rsp < stack_base || initial_rsp > stack_top) {
            vmm_destroy_user_address_space((pte_t *)(uintptr_t)new_cr3);
            return -1;
        }

        memcpy((void *)(uintptr_t)stack_phys, stack_image, stack_image_size);
    } else {
        initial_rsp = stack_top;
    }

    uint64_t old_cr3 = t->cr3;
    t->cr3 = new_cr3;
    t->user_entry = entry;
    t->user_stack_top = stack_top;
    t->brk_base = brk_base;
    t->brk_current = brk_base;
    t->mmap_next = USER_MMAP_BASE;
    if (name)
        strncpy(t->name, name, TASK_NAME_MAX - 1);

    if (t == current_task)
        vmm_switch((pte_t *)(uintptr_t)new_cr3);
    if (old_cr3)
        vmm_destroy_user_address_space((pte_t *)(uintptr_t)old_cr3);

    if (entry_out) *entry_out = entry;
    if (stack_top_out) *stack_top_out = initial_rsp;
    return 0;
}

void task_exit(int code) {
    current_task->exit_code = code;
    current_task->state     = TASK_DEAD;
    schedule();             /* never returns */
    __builtin_unreachable();
}
