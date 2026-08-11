#pragma once

#include "../../include/types.h"
#include "../../include/kernel.h"
#include "../../kernel/fs/vfs.h"

struct syscall_frame;

#define TASK_NAME_MAX   32
#define TASK_STACK_SIZE (8 * 1024)   /* 8 KiB kernel stack per task */
#define MAX_TASKS       64
#define USER_STACK_TOP  0x0000000040100000ULL

typedef enum {
    TASK_READY    = 0,
    TASK_RUNNING  = 1,
    TASK_BLOCKED  = 2,
    TASK_DEAD     = 3,
} task_state_t;

typedef struct task_info {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    uint32_t user_mode;
    uint64_t ticks;
    char     name[TASK_NAME_MAX];
} task_info_t;

typedef struct task {
    uint64_t      rsp;                  /* saved kernel stack pointer  */
    uint64_t      cr3;                  /* page table root (physical)  */
    uint32_t      pid;
    task_state_t  state;
    char          name[TASK_NAME_MAX];
    void         *stack_base;           /* allocated kernel stack      */
    bool          user_mode;
    uint64_t      user_entry;
    uint64_t      user_stack_top;
    uint64_t      brk_base;
    uint64_t      brk_current;
    uint64_t      mmap_next;
    char          cwd[VFS_MAX_PATH];
    file_t        files[VFS_MAX_FDS];
    struct task  *parent;
    struct task  *all_next;
    struct task  *zombie_next;
    int           exit_code;
    uint64_t      ticks;                /* CPU ticks consumed          */
    struct task  *next;                 /* run-queue linkage           */
} task_t;

/* Create a kernel thread. Returns NULL on failure. */
task_t *task_create(const char *name, void (*fn)(void *), void *arg);

/* Create a ring-3 task from an ELF64 executable image. */
task_t *task_create_user_elf(const char *name, const void *image, size_t image_size);

/* Replace an existing ring-3 task image with a new ELF64 executable. */
int task_replace_user_elf(task_t *t, const char *name,
                          const void *image, size_t image_size,
                          const void *stack_image,
                          size_t stack_image_size,
                          uint64_t initial_rsp,
                          uint64_t *entry_out, uint64_t *stack_top_out);

/* Reap parentless dead tasks.  Safe to call from any non-dead task. */
void task_reap_detached_zombies(void);

/* Move a dead task out of the run queue lifecycle and into zombie state. */
void task_zombify(task_t *t);

/* Wait for a child process to exit. */
long task_waitpid(int pid, int *status, int options);

/* Copy a task snapshot by list index. */
long task_get_info(uint32_t index, task_info_t *out);

/* Fork the current user task.  Parent receives child PID; child receives 0. */
long task_fork_from_frame(const struct syscall_frame *frame);

/* Terminate the calling task. */
void __attribute__((noreturn)) task_exit(int code);

/* Current running task. */
extern task_t *current_task;

/* Global PID counter. */
extern uint32_t next_pid;
