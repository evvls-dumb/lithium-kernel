#include "../include/kernel.h"
#include "../lib/kprintf.h"
#include "panic.h"

void _kassert_fail(const char *expr, const char *file, int line) {
    kprintf("\n[ASSERT FAILED] %s\n  at %s:%d\n", expr, file, line);
    panic("Assertion failed");
}
