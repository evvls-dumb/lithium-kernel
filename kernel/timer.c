#include "timer.h"
#include "irq.h"
#include "sched/sched.h"
#include "../include/io.h"
#include "../arch/x86_64/cpu/idt.h"

#define PIT_CH0_DATA 0x40
#define PIT_CMD      0x43

volatile uint64_t jiffies = 0;
static   uint32_t timer_hz = 0;

static void timer_irq(uint8_t irq __attribute__((unused)),
                      interrupt_frame_t *frame __attribute__((unused))) {
    jiffies++;
    /* Cooperative scheduling: sched_tick() not called here.
     * Without proper spinlocks (Phase 13) preempting kprintf corrupts output.
     * Preemptive scheduling re-enabled once spinlocks are in place. */
}

void timer_init(uint32_t hz) {
    timer_hz = hz;
    uint32_t divisor = 1193182 / hz;
    outb(PIT_CMD,       0x36);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)(divisor >> 8));

    irq_register(0, timer_irq);
    irq_unmask(0);
}

uint64_t timer_ms(void) {
    if (!timer_hz) return 0;
    return jiffies * 1000 / timer_hz;
}

void timer_sleep_ms(uint64_t ms) {
    uint64_t deadline = timer_ms() + ms;
    while (timer_ms() < deadline)
        __asm__ volatile ("hlt");
}
