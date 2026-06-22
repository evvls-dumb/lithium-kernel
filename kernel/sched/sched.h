#pragma once

#include "task.h"

/* Initialise the scheduler; must be called after heap_init. */
void sched_init(void);

/* Add a task to the run queue. */
void sched_add(task_t *t);

/* Remove a task from the run queue (called on TASK_DEAD). */
void sched_remove(task_t *t);

/* Pick the next runnable task and switch to it.
 * May be called from the timer IRQ or voluntarily (yield). */
void schedule(void);

/* Voluntary yield — calls schedule(). */
void sched_yield(void);

/* Called from the timer IRQ (every tick). */
void sched_tick(void);

/* Bootstrap the scheduler: create idle task and switch into first real task. */
void sched_start(task_t *first);

/* Assembly: context_switch(&old->rsp, new->rsp) */
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
