#pragma once

#include "../../../include/types.h"

/*
 * 64-bit Task State Segment.
 * The processor reads RSP0 on ring-3 → ring-0 transitions to
 * set the kernel stack pointer before saving user registers.
 */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp[3];        /* RSP0, RSP1, RSP2 (ring stacks) */
    uint64_t reserved1;
    uint64_t ist[7];        /* Interrupt Stack Table entries 1–7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;          /* I/O permission bitmap offset */
} tss_t;

/* Initialise the TSS and set RSP0 to the given kernel stack pointer. */
void   tss_init(void *rsp0);

/* Return a pointer to the kernel TSS (used by gdt_init). */
tss_t *tss_get(void);

/* Set RSP0 — called on every context switch to the new thread's kernel stack. */
void   tss_set_rsp0(uint64_t rsp0);
