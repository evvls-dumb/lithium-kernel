#include "tss.h"
#include "../../../lib/string.h"

static tss_t kernel_tss __attribute__((aligned(16)));

void tss_init(void *rsp0) {
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.rsp[0] = (uint64_t)rsp0;
    /* No IOPB — set offset past end of TSS to disable it. */
    kernel_tss.iopb = (uint16_t)sizeof(tss_t);
}

tss_t *tss_get(void) {
    return &kernel_tss;
}

void tss_set_rsp0(uint64_t rsp0) {
    kernel_tss.rsp[0] = rsp0;
}
