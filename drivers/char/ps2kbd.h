#pragma once

#include "../../include/types.h"

/* Initialise the PS/2 keyboard controller and register IRQ1. */
void kbd_init(void);

/* Return the next ASCII character, sleeping (hlt) until one is available. */
char kbd_getchar(void);

/* Non-blocking: return 0 if the buffer is empty. */
char kbd_trygetchar(void);

/* Poll the PS/2 data port once and enqueue any pending keyboard bytes. */
void kbd_poll(void);

/* Decode one PS/2 scancode byte. Exposed for host-side driver tests. */
bool kbd_decode_scancode(uint8_t scancode, char *out);
void kbd_decode_reset(void);
void kbd_decode_set_scancode_set(uint8_t set);

/* True if at least one character is waiting. */
bool kbd_available(void);

/* Special key state flags. */
extern volatile bool kbd_ctrl_held;
extern volatile bool kbd_alt_held;
