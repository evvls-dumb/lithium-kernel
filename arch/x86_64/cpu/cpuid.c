#include "cpuid.h"
#include "msr.h"
#include "../../../lib/kprintf.h"
#include "../../../lib/string.h"

static cpuid_features_t features;

/* Thin wrapper around the CPUID instruction. */
static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

void cpuid_init(void) {
    uint32_t eax, ebx, ecx, edx;

    memset(&features, 0, sizeof(features));

    /* Leaf 0: vendor string + max basic leaf. */
    cpuid(0, &eax, &ebx, &ecx, &edx);
    features.max_basic = eax;
    memcpy(&features.vendor[0], &ebx, 4);
    memcpy(&features.vendor[4], &edx, 4);
    memcpy(&features.vendor[8], &ecx, 4);
    features.vendor[12] = '\0';

    /* Leaf 1: standard feature flags. */
    if (features.max_basic >= 1) {
        cpuid(1, &eax, &ebx, &ecx, &edx);
        features.has_apic   = (edx >> 9)  & 1;
        features.has_sse    = (edx >> 25) & 1;
        features.has_sse2   = (edx >> 26) & 1;
        features.has_x2apic = (ecx >> 21) & 1;
        features.has_avx    = (ecx >> 28) & 1;
    }

    /* Leaf 7: extended features (AVX2). */
    if (features.max_basic >= 7) {
        cpuid(7, &eax, &ebx, &ecx, &edx);
        features.has_avx2 = (ebx >> 5) & 1;
    }

    /* Extended leaf 0x80000000: max extended leaf. */
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    features.max_extended = eax;

    /* Extended leaf 0x80000001: NX, SYSCALL, 1 GiB pages. */
    if (features.max_extended >= 0x80000001) {
        cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        features.has_syscall   = (edx >> 11) & 1;
        features.has_nx        = (edx >> 20) & 1;
        features.has_1gb_pages = (edx >> 26) & 1;
    }
}

const cpuid_features_t *cpuid_features(void) {
    return &features;
}

void cpuid_print_features(void) {
    kprintf("CPU vendor  : %s\n", features.vendor);
    kprintf("Max leaf    : 0x%08x (ext: 0x%08x)\n",
            features.max_basic, features.max_extended);
    kprintf("Features    :%s%s%s%s%s%s%s%s%s\n",
            features.has_apic      ? " APIC"    : "",
            features.has_x2apic    ? " x2APIC"  : "",
            features.has_sse       ? " SSE"     : "",
            features.has_sse2      ? " SSE2"    : "",
            features.has_avx       ? " AVX"     : "",
            features.has_avx2      ? " AVX2"    : "",
            features.has_nx        ? " NX"      : "",
            features.has_syscall   ? " SYSCALL" : "",
            features.has_1gb_pages ? " 1GB-PG"  : "");
}
