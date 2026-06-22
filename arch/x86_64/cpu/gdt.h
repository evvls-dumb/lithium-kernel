#pragma once

#include "../../../include/types.h"

/* Segment selector byte offsets in the GDT. */
#define GDT_NULL        0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18   /* Use 0x1B (|3) when placing in CS for ring-3 */
#define GDT_USER_DATA   0x20   /* Use 0x23 (|3) when placing in SS for ring-3 */
#define GDT_TSS         0x28   /* TSS occupies two slots (16 bytes) */

/* Standard 8-byte descriptor. */
typedef struct __attribute__((packed)) {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_lim_hi;  /* [7:4] flags, [3:0] limit[19:16] */
    uint8_t  base_hi;
} gdt_entry_t;

/* 64-bit system descriptor (TSS / LDT) is 16 bytes. */
typedef struct __attribute__((packed)) {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_lim_hi;
    uint8_t  base_hi;
    uint32_t base_upper;
    uint32_t reserved;
} gdt_sys_entry_t;

/* GDTR value passed to lgdt. */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

/* Build and load the full kernel GDT (null, kcode, kdata, ucode, udata, TSS). */
void gdt_init(void);

/* Assembly helper — loads GDTR and reloads all segment registers. */
void gdt_load(const gdtr_t *gdtr);

/* Assembly helper — loads the TSS selector into TR. */
void tss_load(uint16_t selector);
