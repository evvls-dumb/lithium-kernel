#pragma once

#include "../../../include/types.h"

/* ── Interrupt/exception frame ──────────────────────────────────
 *
 * Stack layout on entry to isr_common_stub (lowest addr first):
 *
 *   [rsp+ 0]  r15 )
 *   [rsp+ 8]  r14 )
 *   ...             ← pushed by isr_common_stub
 *   [rsp+112] rax )
 *   [rsp+120] vector      ← pushed by per-vector ISR stub
 *   [rsp+128] error_code  ← CPU-pushed (or dummy 0)
 *   [rsp+136] rip    )
 *   [rsp+144] cs     )
 *   [rsp+152] rflags )    ← pushed by CPU on interrupt entry
 *   [rsp+160] rsp    )
 *   [rsp+168] ss     )
 */
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

/* ── IDT gate descriptor ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint16_t offset_lo;
    uint16_t cs;
    uint8_t  ist;       /* bits 2:0 = IST index (0 = disabled) */
    uint8_t  flags;     /* type + DPL + present */
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

/* Gate type / flag bits. */
#define IDT_INT_GATE    0x0E    /* 64-bit interrupt gate (clears IF on entry) */
#define IDT_TRAP_GATE   0x0F    /* 64-bit trap gate (IF unchanged)            */
#define IDT_PRESENT     0x80

/* Initialise the IDT with 256 stubs and load it. */
void idt_init(void);

/* The C-level dispatcher called by every ISR stub. */
void isr_handler(interrupt_frame_t *frame);
