#pragma once

#include "../include/types.h"

/* Initialise PIT channel 0 at the given frequency (Hz). */
void timer_init(uint32_t hz);

/* Milliseconds elapsed since timer_init(). */
uint64_t timer_ms(void);

/* Sleep for approximately the given number of milliseconds. */
void timer_sleep_ms(uint64_t ms);

/* Tick count (incremented by the IRQ0 handler). */
extern volatile uint64_t jiffies;
