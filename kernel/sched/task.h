#pragma once

#include "../../include/types.h"
#include "../../include/kernel.h"

#define TASK_NAME_MAX   32
#define TASK_STACK_SIZE (8 * 1024)   /* 8 KiB kernel stack per task */
#define MAX_TASKS       64

typedef enum {
    TASK_READY    = 0,
    TASK_RUNNING  = 1,
    TASK_BLOCKED  = 2,
    TASK_DEAD     = 3,
} task_state_t;

typedef struct task {
    uint64_t      rsp;                  /* saved kernel stack pointer  */
    uint64_t      cr3;                  /* page table root (physical)  */
    uint32_t      pid;
    task_state_t  state;
    char          name[TASK_NAME_MAX];
    void         *stack_base;           /* allocated kernel stack      */
    int           exit_code;
    uint64_t      ticks;                /* CPU ticks consumed          */
    struct task  *next;                 /* run-queue linkage           */
} task_t;

/* Create a kernel thread. Returns NULL on failure. */
task_t *task_create(const char *name, void (*fn)(void *), void *arg);

/* Terminate the calling task. */
void __attribute__((noreturn)) task_exit(int code);

/* Current running task. */
extern task_t *current_task;

/* Global PID counter. */
extern uint32_t next_pid;
