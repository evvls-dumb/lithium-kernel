#pragma once

#include "../../../include/types.h"

/* Well-known MSR addresses used by the kernel. */
#define MSR_EFER    0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR    0xC0000081  /* SYSCALL segment selectors        */
#define MSR_LSTAR   0xC0000082  /* SYSCALL 64-bit entry RIP         */
#define MSR_FMASK   0xC0000084  /* SYSCALL RFLAGS mask              */

/* EFER bit positions. */
#define EFER_SCE    (1UL << 0)   /* SYSCALL Enable                  */
#define EFER_LME    (1UL << 8)   /* Long Mode Enable (read-only)    */
#define EFER_LMA    (1UL << 10)  /* Long Mode Active (read-only)    */
#define EFER_NXE    (1UL << 11)  /* No-Execute Enable               */

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr"
        : : "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}
