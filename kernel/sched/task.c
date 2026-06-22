#include "task.h"
#include "sched.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/mm/vmm.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"
#include "../../kernel/panic.h"

task_t  *current_task = NULL;
uint32_t next_pid     = 1;

/*
 * Initial stack frame for a newly created task.
 * context_switch (in context.asm) pops these in order:
 *   r15, r14, r13, r12, rbp, rbx  (pushed by context_switch on first call)
 * and then ret → task_entry_trampoline.
 */
/* Declared in context.asm — used as the return address for new tasks. */
extern void task_trampoline(void);

task_t *task_create(const char *name, void (*fn)(void *), void *arg) {
    task_t *t = (task_t *)kzalloc(sizeof(task_t));
    if (!t) return NULL;

    t->stack_base = kmalloc(TASK_STACK_SIZE);
    if (!t->stack_base) { kfree(t); return NULL; }

    t->pid   = next_pid++;
    t->state = TASK_READY;
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
    uint64_t *stk = (uint64_t *)((uint8_t *)t->stack_base + TASK_STACK_SIZE);

    *--stk = (uint64_t)(uintptr_t)task_trampoline; /* ret target           */
    *--stk = 0;                                     /* r15                  */
    *--stk = 0;                                     /* r14                  */
    *--stk = (uint64_t)(uintptr_t)arg;              /* r13 = arg            */
    *--stk = (uint64_t)(uintptr_t)fn;               /* r12 = fn             */
    *--stk = 0;                                     /* rbp                  */
    *--stk = 0;                                     /* rbx                  */

    t->rsp = (uint64_t)(uintptr_t)stk;

    sched_add(t);
    return t;
}

void task_exit(int code) {
    current_task->exit_code = code;
    current_task->state     = TASK_DEAD;
    schedule();             /* never returns */
    __builtin_unreachable();
}
