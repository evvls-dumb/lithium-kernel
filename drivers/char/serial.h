#pragma once

#include "../../include/types.h"

/* COM1 UART — used as a secondary debug output readable from the host via
   `qemu -serial stdio` or `-serial file:serial.log`. */

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *s);
bool serial_ready(void);
