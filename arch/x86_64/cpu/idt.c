#include "idt.h"
#include "gdt.h"
#include "../../../include/io.h"
#include "../../../include/kernel.h"
#include "../../../lib/string.h"
#include "../../../kernel/panic.h"
#include "../../../kernel/irq.h"
#include "../../../kernel/mm/vmm.h"

extern uint64_t isr_table[256];

static idt_entry_t idt[256] __attribute__((aligned(16)));
static idtr_t      idtr;

/* ── PIC 8259 ────────────────────────────────────────────────────── */
#define PIC_MASTER_CMD  0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_CMD   0xA0
#define PIC_SLAVE_DATA  0xA1
#define PIC_EOI         0x20

static void pic_remap(void) {
    outb(PIC_MASTER_CMD,  0x11); io_wait();
    outb(PIC_SLAVE_CMD,   0x11); io_wait();
    outb(PIC_MASTER_DATA, 0x20); io_wait();   /* IRQ 0-7  → 0x20 */
    outb(PIC_SLAVE_DATA,  0x28); io_wait();   /* IRQ 8-15 → 0x28 */
    outb(PIC_MASTER_DATA, 0x04); io_wait();
    outb(PIC_SLAVE_DATA,  0x02); io_wait();
    outb(PIC_MASTER_DATA, 0x01); io_wait();
    outb(PIC_SLAVE_DATA,  0x01); io_wait();
    outb(PIC_MASTER_DATA, 0xFF); /* all masked — drivers unmask selectively */
    outb(PIC_SLAVE_DATA,  0xFF);
}

static void idt_set_entry(int vec, uint64_t handler,
                          uint16_t cs, uint8_t flags, uint8_t ist) {
    idt[vec].offset_lo  = (uint16_t)(handler & 0xFFFF);
    idt[vec].cs         = cs;
    idt[vec].ist        = ist & 0x07;
    idt[vec].flags      = flags;
    idt[vec].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vec].offset_hi  = (uint32_t)(handler >> 32);
    idt[vec].reserved   = 0;
}

void idt_init(void) {
    pic_remap();
    for (int i = 0; i < 256; i++) {
        uint8_t type = (i == 3) ? IDT_TRAP_GATE : IDT_INT_GATE;
        idt_set_entry(i, isr_table[i], GDT_KERNEL_CODE, IDT_PRESENT | type, 0);
    }
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint64_t)&idt[0];
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

/* ── IRQ dispatch (called from isr_handler for vectors 32-47) ─────── */

static irq_handler_t irq_handlers[16];

void irq_register(uint8_t irq, irq_handler_t handler) {
    KASSERT(irq < 16);
    irq_handlers[irq] = handler;
}

void irq_unregister(uint8_t irq) {
    KASSERT(irq < 16);
    irq_handlers[irq] = NULL;
}

void irq_mask(uint8_t irq) {
    if (irq < 8) {
        outb(PIC_MASTER_DATA, inb(PIC_MASTER_DATA) |  (uint8_t)(1u << irq));
    } else {
        outb(PIC_SLAVE_DATA,  inb(PIC_SLAVE_DATA)  |  (uint8_t)(1u << (irq - 8)));
    }
}

void irq_unmask(uint8_t irq) {
    if (irq < 8) {
        outb(PIC_MASTER_DATA, inb(PIC_MASTER_DATA) & ~(uint8_t)(1u << irq));
        /* Also unmask IRQ2 (cascade) on master when enabling any slave IRQ. */
    } else {
        outb(PIC_SLAVE_DATA,  inb(PIC_SLAVE_DATA)  & ~(uint8_t)(1u << (irq - 8)));
        outb(PIC_MASTER_DATA, inb(PIC_MASTER_DATA) & ~(uint8_t)(1u << 2)); /* cascade */
    }
}

void irq_dispatch(uint8_t irq, interrupt_frame_t *frame) {
    if (irq < 16 && irq_handlers[irq])
        irq_handlers[irq](irq, frame);
}

/* ── Top-level interrupt dispatcher ─────────────────────────────── */

static void pic_eoi(uint8_t vector) {
    if (vector >= 40) outb(PIC_SLAVE_CMD,  PIC_EOI);
    outb(PIC_MASTER_CMD, PIC_EOI);
}

void isr_handler(interrupt_frame_t *frame) {
    if (frame->vector < 32) {
        if (frame->vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            if (vmm_handle_page_fault(cr2, frame->error_code))
                return;
        }
        panic_exception(frame);
    } else if (frame->vector < 48) {
        uint8_t irq = (uint8_t)(frame->vector - 32);
        irq_dispatch(irq, frame);
        pic_eoi(frame->vector);
    }
    /* 48-255: software/syscall — handled in Phase 8 */
}
