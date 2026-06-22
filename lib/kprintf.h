#pragma once

#include "../include/types.h"

/* Kernel printf — writes to VGA text buffer and COM1 serial simultaneously.
   Supports: %d %i %u %x %X %p %s %c %% with optional width and zero-pad. */
void kprintf(const char *fmt, ...);

/* Emit a single character to both outputs. */
void kputchar(char c);
