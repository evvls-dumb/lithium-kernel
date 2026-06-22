#pragma once

#include "../../../include/types.h"

/* Feature flags detected at boot. */
typedef struct {
    bool has_apic;
    bool has_x2apic;
    bool has_sse;
    bool has_sse2;
    bool has_avx;
    bool has_avx2;
    bool has_nx;        /* No-Execute (EFER.NXE capable) */
    bool has_syscall;   /* SYSCALL / SYSRET instructions  */
    bool has_1gb_pages; /* 1 GiB huge-page support        */
    uint32_t max_basic;
    uint32_t max_extended;
    char vendor[13];    /* null-terminated vendor string  */
} cpuid_features_t;

/* Run CPUID and populate the global feature struct. */
void cpuid_init(void);

/* Return a pointer to the populated feature struct. */
const cpuid_features_t *cpuid_features(void);

/* Print a human-readable summary to kprintf. */
void cpuid_print_features(void);
