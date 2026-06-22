#pragma once

#include "../../include/types.h"

/*
 * Physical Memory Manager — bitmap allocator.
 *
 * Manages physical 4 KiB page frames.  One bit per frame:
 *   0 = free, 1 = used.
 *
 * Supports up to PMM_MAX_PHYS bytes of addressable physical memory.
 * The bitmap itself lives in .bss (zero-cost until used).
 */

#define PMM_FRAME_SIZE      4096ULL
#define PMM_MAX_PHYS        (4ULL * 1024 * 1024 * 1024)   /* 4 GiB */
#define PMM_MAX_FRAMES      (PMM_MAX_PHYS / PMM_FRAME_SIZE) /* 1 Mi frames */
#define PMM_BITMAP_BYTES    (PMM_MAX_FRAMES / 8)            /* 128 KiB     */

/* Returned by pmm_alloc_* when no frame is available. */
#define PMM_ALLOC_FAILED    (~0ULL)

/*
 * Initialise the PMM from the bootloader-provided memory map.
 * Must be called before any alloc/free.
 *
 *   boot_magic    — value in EAX on kernel entry (identifies protocol)
 *   boot_info_phys — value in EBX on kernel entry (MB2 info / PVH start_info)
 */
void pmm_init(uint32_t boot_magic, uint32_t boot_info_phys);

/*
 * Allocate / free a single 4 KiB physical frame.
 * pmm_alloc_frame returns the physical base address, or PMM_ALLOC_FAILED.
 */
uint64_t pmm_alloc_frame(void);
void     pmm_free_frame(uint64_t phys_addr);

/*
 * Allocate `count` physically *contiguous* 4 KiB frames (for DMA buffers).
 * Returns base physical address of the run, or PMM_ALLOC_FAILED.
 */
uint64_t pmm_alloc_contiguous(uint32_t count);

/* Statistics. */
uint64_t pmm_total_bytes(void);
uint64_t pmm_free_bytes(void);
void     pmm_stats(void);

/* Boot-time self-test — allocates and frees 1000 frames, verifies counts. */
void pmm_selftest(void);
