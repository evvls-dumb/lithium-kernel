#pragma once

#include "../arch/x86_64/cpu/idt.h"

/* Print a message and hard-halt the system. Never returns. */
void panic(const char *msg) __attribute__((noreturn));

/* Print a full register dump for an unhandled CPU exception. Never returns. */
void panic_exception(interrupt_frame_t *frame) __attribute__((noreturn));
