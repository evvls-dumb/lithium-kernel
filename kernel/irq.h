#pragma once

#include "../include/types.h"
#include "../arch/x86_64/cpu/idt.h"

typedef void (*irq_handler_t)(uint8_t irq, interrupt_frame_t *frame);

/* Register/unregister a handler for hardware IRQ line 0–15. */
void irq_register  (uint8_t irq, irq_handler_t handler);
void irq_unregister(uint8_t irq);

/* Mask / unmask an IRQ line at the 8259 PIC. */
void irq_mask  (uint8_t irq);
void irq_unmask(uint8_t irq);

/* Called from isr_handler for vectors 32–47. */
void irq_dispatch(uint8_t irq, interrupt_frame_t *frame);
