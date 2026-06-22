#include "gdt.h"
#include "tss.h"
#include "../../../lib/string.h"

/*
 * GDT layout (7 entries = 5 standard + 2-slot TSS):
 *
 *  Index  Offset  Description
 *  ──────────────────────────────────────────────────────────
 *    0    0x00    Null descriptor (required by x86)
 *    1    0x08    Kernel code  (DPL=0, L=1, 64-bit)
 *    2    0x10    Kernel data  (DPL=0)
 *    3    0x18    User code    (DPL=3, L=1, 64-bit)
 *    4    0x20    User data    (DPL=3)
 *    5    0x28    TSS low  (16-byte system descriptor, slot 0)
 *    6    0x30    TSS high (slot 1 — upper base bits)
 */
#define GDT_ENTRY_COUNT 7

static gdt_entry_t gdt[GDT_ENTRY_COUNT] __attribute__((aligned(8)));
static gdtr_t      gdtr;

/* ── Helper: fill one 8-byte descriptor ─────────────────────── */
static void set_entry(int idx, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags)
{
    gdt[idx].limit_lo    = (uint16_t)(limit & 0xFFFF);
    gdt[idx].base_lo     = (uint16_t)(base  & 0xFFFF);
    gdt[idx].base_mid    = (uint8_t)((base  >> 16) & 0xFF);
    gdt[idx].access      = access;
    /* flags nibble in [7:4], upper limit nibble in [3:0] */
    gdt[idx].flags_lim_hi = (uint8_t)(((flags & 0x0F) << 4) |
                                       ((limit >> 16) & 0x0F));
    gdt[idx].base_hi     = (uint8_t)((base >> 24) & 0xFF);
}

/* ── Helper: fill a 16-byte system descriptor (TSS/LDT) ──────── */
static void set_sys_entry(int idx, uint64_t base, uint32_t limit,
                          uint8_t access)
{
    gdt_sys_entry_t *e = (gdt_sys_entry_t *)&gdt[idx];
    e->limit_lo    = (uint16_t)(limit & 0xFFFF);
    e->base_lo     = (uint16_t)(base  & 0xFFFF);
    e->base_mid    = (uint8_t)((base  >> 16) & 0xFF);
    e->access      = access;
    e->flags_lim_hi = (uint8_t)((limit >> 16) & 0x0F);  /* G=0, limit only */
    e->base_hi     = (uint8_t)((base  >> 24) & 0xFF);
    e->base_upper  = (uint32_t)(base  >> 32);
    e->reserved    = 0;
}

void gdt_init(void) {
    /*
     * Access byte encoding:
     *   Bit 7   : Present
     *   Bits 6:5: DPL
     *   Bit 4   : S  (1 = code/data, 0 = system)
     *   Bits 3:0: Type
     *     Code: 1010 = execute/read
     *     Data: 0010 = read/write
     *
     * Flags nibble (placed in bits 7:4 of flags_lim_hi):
     *   Bit 3 (G)  : granularity (1 = 4 KiB, 0 = byte)
     *   Bit 2 (D/B): default op-size (0 for 64-bit code)
     *   Bit 1 (L)  : long-mode code (1 = 64-bit)
     *   Bit 0      : available
     */

    /* 0: Null */
    set_entry(0, 0, 0, 0, 0);

    /* 1: Kernel code  — P=1, DPL=0, S=1, Type=0xA, G=1, L=1 → flags=0xA */
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xA);

    /* 2: Kernel data  — P=1, DPL=0, S=1, Type=0x2, G=1, D=1 → flags=0xC */
    set_entry(2, 0, 0xFFFFF, 0x92, 0xC);

    /* 3: User code    — P=1, DPL=3, S=1, Type=0xA, G=1, L=1 */
    set_entry(3, 0, 0xFFFFF, 0xFA, 0xA);

    /* 4: User data    — P=1, DPL=3, S=1, Type=0x2, G=1, D=1 */
    set_entry(4, 0, 0xFFFFF, 0xF2, 0xC);

    /* 5-6: TSS        — P=1, DPL=0, S=0, Type=0x9 (64-bit available TSS) */
    tss_t *tss = tss_get();
    set_sys_entry(5, (uint64_t)tss, (uint32_t)(sizeof(tss_t) - 1), 0x89);

    /* Point GDTR at our table. */
    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base  = (uint64_t)&gdt[0];

    /* Load GDTR and reload all segment registers (in assembly). */
    gdt_load(&gdtr);

    /* Load TSS selector into TR. */
    tss_load(GDT_TSS);
}
