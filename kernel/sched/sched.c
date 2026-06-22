#include "sched.h"
#include "task.h"
#include "../../arch/x86_64/cpu/tss.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"
#include "../../kernel/panic.h"

static task_t *run_queue_head = NULL;
static uint32_t task_count    = 0;
static task_t  *idle_task     = NULL;

static void idle_fn(void *arg __attribute__((unused))) {
    for (;;) __asm__ volatile ("sti; hlt");
}

void sched_init(void) {
    run_queue_head = NULL;
    task_count     = 0;
}

void sched_add(task_t *t) {
    uint64_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));
    if (!run_queue_head) {
        t->next = t;
        run_queue_head = t;
    } else {
        t->next = run_queue_head->next;
        run_queue_head->next = t;
    }
    task_count++;
    __asm__ volatile ("push %0; popf" : : "r"(flags));
}

void sched_remove(task_t *t) {
    if (!run_queue_head || task_count == 0) return;
    task_t *prev = run_queue_head;
    while (prev->next != t && prev->next != run_queue_head) prev = prev->next;
    if (prev->next == t) {
        if (t->next == t) {
            run_queue_head = NULL;
        } else {
            prev->next = t->next;
            if (run_queue_head == t) run_queue_head = t->next;
        }
        task_count--;
    }
}

void schedule(void) {
    /*
     * Keep interrupts DISABLED throughout the switch.
     * - When called from the timer ISR:  IF is already 0 (ISR entry clears IF).
     *   The resumed task re-enables IF naturally via iretq restoring RFLAGS.
     * - When called from sched_yield():  we cli here, context_switch happens,
     *   and the resumed task's saved RFLAGS (which had IF=1) is restored by
     *   whatever iretq eventually follows.
     * - NEW tasks: task_trampoline does an explicit sti after startup.
     *
     * Keeping cli around context_switch prevents a double-scheduling race
     * where a second timer IRQ fires between the sti and the actual switch.
     */
    __asm__ volatile ("cli");

    if (!current_task) {
        __asm__ volatile ("sti");
        return;
    }

    /* Round-robin: find next READY task. */
    task_t *next = current_task->next;
    for (uint32_t i = 0; i < task_count; i++) {
        if (next && (next->state == TASK_READY || next->state == TASK_RUNNING))
            break;
        if (next) next = next->next;
    }

    /* Retire dead task. */
    if (current_task->state == TASK_DEAD) {
        sched_remove(current_task);
    } else if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }

    if (!next || (next->state != TASK_READY && next->state != TASK_RUNNING))
        next = idle_task;

    if (!next || next == current_task) {
        if (current_task) current_task->state = TASK_RUNNING;
        __asm__ volatile ("sti");
        return;
    }

    tss_set_rsp0((uint64_t)(uintptr_t)next->stack_base + TASK_STACK_SIZE);

    task_t *old  = current_task;
    current_task = next;
    next->state  = TASK_RUNNING;

    /* context_switch runs with IF=0; the new task enables IF on its own. */
    context_switch(&old->rsp, next->rsp);
    /* When context_switch returns here, the OLD task has been resumed
     * (we're back in the ISR path) — IF is still 0, iretq will restore it. */
}

void sched_yield(void) { schedule(); }

void sched_tick(void) {
    if (current_task) current_task->ticks++;
    schedule();
}

void sched_start(task_t *first) {
    idle_task = task_create("idle", idle_fn, NULL);

    static task_t bootstrap;
    memset(&bootstrap, 0, sizeof(bootstrap));
    bootstrap.state = TASK_DEAD;
    strncpy(bootstrap.name, "bootstrap", TASK_NAME_MAX - 1);

    current_task       = first;
    first->state       = TASK_RUNNING;

    context_switch(&bootstrap.rsp, first->rsp);
    /* Never reached. */
    for (;;) __asm__ volatile ("hlt");
}
