#pragma once

#include "../../include/types.h"
#include "pmm.h"

/*
 * Virtual Memory Manager — 4-level (PML4) page-table manager.
 *
 * Address space layout (Phase 3):
 *
 *   0x0000 0000 0000 0000 – 0x0000 7FFF FFFF FFFF  user space (lower half)
 *   0xFFFF 8000 0000 0000 – 0xFFFF FFFF 7FFF FFFF  (non-canonical hole)
 *   0xFFFF FFFF 8000 0000 – 0xFFFF FFFF FFFF FFFF  kernel space (higher half)
 *
 * Phase 3 maps:
 *   Identity   0x0001 0000 – end-of-RAM  (4 KiB pages for 0–2 MiB, 2 MiB huge pages after)
 *   Higher-half kernel alias at KERNEL_VMA + phys
 *   NULL page (0x0–0xFFF) intentionally unmapped
 *   Guard page below kernel stack intentionally unmapped
 */

#define KERNEL_VMA   0xFFFFFFFF80000000ULL   /* higher-half kernel base */
#define PAGE_SIZE    4096ULL

/* Page-table entry flags (Intel Vol 3, Table 4-5). */
#define VMM_PRESENT  (1ULL << 0)
#define VMM_WRITE    (1ULL << 1)
#define VMM_USER     (1ULL << 2)
#define VMM_PWT      (1ULL << 3)
#define VMM_PCD      (1ULL << 4)
#define VMM_ACCESSED (1ULL << 5)
#define VMM_DIRTY    (1ULL << 6)
#define VMM_HUGE     (1ULL << 7)   /* PS bit — 2 MiB (PD) or 1 GiB (PDPT) page */
#define VMM_GLOBAL   (1ULL << 8)
#define VMM_NX       (1ULL << 63)  /* No-Execute (EFER.NXE must be set)         */

/* Mask to extract the physical base address from an entry. */
#define VMM_PHYS_MASK  0x000FFFFFFFFFF000ULL

typedef uint64_t pte_t;

/* ── Lifecycle ────────────────────────────────────────────────── */

/* Build proper 4-level page tables, unmap null page + guard page,
 * set up higher-half kernel alias, and activate. */
void vmm_init(void);

/* ── Address-space operations ─────────────────────────────────── */

/* Map a single 4 KiB page in address space pml4.
 * flags: combination of VMM_WRITE, VMM_USER, VMM_NX, etc.
 * Returns 0 on success, -1 if a page-table frame could not be allocated. */
int vmm_map_page(pte_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a 4 KiB page and invalidate its TLB entry.
 * No-op if the page is already unmapped or covered by a huge page. */
void vmm_unmap_page(pte_t *pml4, uint64_t virt);

/* Walk the page tables to find the physical address backing virt.
 * Handles 2 MiB huge pages.  Returns PMM_ALLOC_FAILED if not mapped. */
uint64_t vmm_get_physical(pte_t *pml4, uint64_t virt);

/* Allocate a new PML4 with the kernel half (entries 256–511) shared
 * from the current address space.  Used for new processes. */
pte_t *vmm_create_address_space(void);

/* Write pml4's physical address to CR3 (flushes TLB). */
void vmm_switch(pte_t *pml4);

/* Return a pointer to the currently active PML4 (from CR3). */
pte_t *vmm_current_pml4(void);

/* ── Kernel virtual-address allocator ─────────────────────────── */

/* Return the next available kernel virtual address of the given size
 * (page-aligned, bump-pointer style).  Caller must map physical frames. */
uint64_t vmm_alloc_kernel(uint64_t size);
