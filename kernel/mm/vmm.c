#include "vmm.h"
#include "pmm.h"
#include "../../include/kernel.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"
#include "../../kernel/panic.h"

/* Linker-script symbols. */
extern char _kernel_end[];
extern char stack_bottom[];   /* defined in entry.asm BSS */

/* The kernel's root page table (physical == virtual via identity map). */
static pte_t *kernel_pml4 = NULL;

/* Kernel virtual-address bump allocator — starts just above the kernel image
 * in the higher-half mapping. */
static uint64_t kheap_next = 0;

/* ── Internal helpers ────────────────────────────────────────── */

/*
 * Given a pointer to a page-table entry (*entry), return a pointer to the
 * next-level table it describes — allocating a fresh frame if needed.
 * The new frame is identity-mapped (physical == virtual) so direct C
 * pointer use is safe throughout Phase 3.
 */
static pte_t *pt_walk_or_alloc(pte_t *entry) {
    if (*entry & VMM_PRESENT) {
        return (pte_t *)(uintptr_t)(*entry & VMM_PHYS_MASK);
    }
    uint64_t phys = pmm_alloc_frame();
    if (phys == PMM_ALLOC_FAILED) return NULL;
    memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);
    *entry = phys | VMM_PRESENT | VMM_WRITE;
    return (pte_t *)(uintptr_t)phys;
}

static inline int pml4_idx(uint64_t v) { return (int)((v >> 39) & 0x1FF); }
static inline int pdpt_idx(uint64_t v) { return (int)((v >> 30) & 0x1FF); }
static inline int pd_idx  (uint64_t v) { return (int)((v >> 21) & 0x1FF); }
static inline int pt_idx  (uint64_t v) { return (int)((v >> 12) & 0x1FF); }

/* ── Public API ──────────────────────────────────────────────── */

void vmm_switch(pte_t *pml4) {
    __asm__ volatile ("mov %0, %%cr3"
        : : "r"((uint64_t)(uintptr_t)pml4) : "memory");
}

pte_t *vmm_current_pml4(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return (pte_t *)(uintptr_t)(cr3 & ~0xFFFULL);
}

int vmm_map_page(pte_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    pte_t *pdpt = pt_walk_or_alloc(&pml4[pml4_idx(virt)]);
    if (!pdpt) return -1;

    pte_t *pd = pt_walk_or_alloc(&pdpt[pdpt_idx(virt)]);
    if (!pd) return -1;

    /* Refuse to silently split a 2 MiB huge page. */
    if (pd[pd_idx(virt)] & VMM_HUGE) return -1;

    pte_t *pt = pt_walk_or_alloc(&pd[pd_idx(virt)]);
    if (!pt) return -1;

    pt[pt_idx(virt)] = (phys & VMM_PHYS_MASK) | (flags & ~VMM_PHYS_MASK) | VMM_PRESENT;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void vmm_unmap_page(pte_t *pml4, uint64_t virt) {
    if (!(pml4[pml4_idx(virt)] & VMM_PRESENT)) return;
    pte_t *pdpt = (pte_t *)(uintptr_t)(pml4[pml4_idx(virt)] & VMM_PHYS_MASK);

    if (!(pdpt[pdpt_idx(virt)] & VMM_PRESENT)) return;
    pte_t *pd = (pte_t *)(uintptr_t)(pdpt[pdpt_idx(virt)] & VMM_PHYS_MASK);

    /* Skip if huge page or not present. */
    if (!(pd[pd_idx(virt)] & VMM_PRESENT) || (pd[pd_idx(virt)] & VMM_HUGE)) return;
    pte_t *pt = (pte_t *)(uintptr_t)(pd[pd_idx(virt)] & VMM_PHYS_MASK);

    pt[pt_idx(virt)] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_get_physical(pte_t *pml4, uint64_t virt) {
    if (!(pml4[pml4_idx(virt)] & VMM_PRESENT)) return PMM_ALLOC_FAILED;
    pte_t *pdpt = (pte_t *)(uintptr_t)(pml4[pml4_idx(virt)] & VMM_PHYS_MASK);

    if (!(pdpt[pdpt_idx(virt)] & VMM_PRESENT)) return PMM_ALLOC_FAILED;
    if (pdpt[pdpt_idx(virt)] & VMM_HUGE)   /* 1 GiB page */
        return (pdpt[pdpt_idx(virt)] & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);
    pte_t *pd = (pte_t *)(uintptr_t)(pdpt[pdpt_idx(virt)] & VMM_PHYS_MASK);

    if (!(pd[pd_idx(virt)] & VMM_PRESENT)) return PMM_ALLOC_FAILED;
    if (pd[pd_idx(virt)] & VMM_HUGE)       /* 2 MiB page */
        return (pd[pd_idx(virt)] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);
    pte_t *pt = (pte_t *)(uintptr_t)(pd[pd_idx(virt)] & VMM_PHYS_MASK);

    if (!(pt[pt_idx(virt)] & VMM_PRESENT)) return PMM_ALLOC_FAILED;
    return (pt[pt_idx(virt)] & VMM_PHYS_MASK) | (virt & 0xFFFULL);
}

pte_t *vmm_create_address_space(void) {
    uint64_t phys = pmm_alloc_frame();
    if (phys == PMM_ALLOC_FAILED) return NULL;
    pte_t *new_pml4 = (pte_t *)(uintptr_t)phys;
    memset(new_pml4, 0, PAGE_SIZE);

    /* Share the kernel half (PML4[256..511]) — same page tables, no copy. */
    pte_t *cur = vmm_current_pml4();
    for (int i = 256; i < 512; i++)
        new_pml4[i] = cur[i];

    return new_pml4;
}

uint64_t vmm_alloc_kernel(uint64_t size) {
    size = ALIGN_UP(size, PAGE_SIZE);
    uint64_t addr = kheap_next;
    kheap_next += size;
    return addr;
}

/* ── vmm_init ────────────────────────────────────────────────── */

void vmm_init(void) {
    /* Physical page just below the kernel stack — becomes the guard page. */
    uint64_t guard_phys = (uint64_t)(uintptr_t)stack_bottom - PAGE_SIZE;

    kprintf("VMM: guard page at phys 0x%lx (below stack_bottom 0x%lx)\n",
            guard_phys, (uint64_t)(uintptr_t)stack_bottom);

    /* ── 1. Allocate new PML4 ──────────────────────────────── */
    uint64_t pml4_phys = pmm_alloc_frame();
    KASSERT(pml4_phys != PMM_ALLOC_FAILED);
    pte_t *pml4 = (pte_t *)(uintptr_t)pml4_phys;
    memset(pml4, 0, PAGE_SIZE);

    /* ── 2. Identity map 0–1 GiB ───────────────────────────
     *
     * First 2 MiB: 4 KiB pages so we can punch exact holes
     *   - PT[0]          = unmapped  (null-page protection)
     *   - PT[guard_phys/PAGE_SIZE] = unmapped  (stack guard)
     *   - PT[1..511 else] = identity
     *
     * 2 MiB – 1 GiB: 2 MiB huge pages (PD[1..511])
     */

    /* Allocate PT for [0, 2 MiB). */
    uint64_t pt0_phys = pmm_alloc_frame();
    KASSERT(pt0_phys != PMM_ALLOC_FAILED);
    pte_t *pt0 = (pte_t *)(uintptr_t)pt0_phys;
    memset(pt0, 0, PAGE_SIZE);

    for (int i = 1; i < 512; i++) {          /* i=0 → null page, stays 0 */
        uint64_t phys = (uint64_t)i * PAGE_SIZE;
        if (phys == guard_phys) continue;    /* guard page, stays 0       */
        pt0[i] = phys | VMM_PRESENT | VMM_WRITE;
    }

    /* Allocate PD for identity PDPT[0]. */
    uint64_t pd0_phys = pmm_alloc_frame();
    KASSERT(pd0_phys != PMM_ALLOC_FAILED);
    pte_t *pd0 = (pte_t *)(uintptr_t)pd0_phys;
    memset(pd0, 0, PAGE_SIZE);

    pd0[0] = pt0_phys | VMM_PRESENT | VMM_WRITE; /* [0–2 MiB)  via 4 KiB PT  */
    for (int i = 1; i < 512; i++) {              /* [2 MiB–1 GiB) huge pages */
        pd0[i] = ((uint64_t)i * 0x200000ULL) | VMM_PRESENT | VMM_WRITE | VMM_HUGE;
    }

    /* Allocate PDPT for identity (PML4[0]). */
    uint64_t pdpt_id_phys = pmm_alloc_frame();
    KASSERT(pdpt_id_phys != PMM_ALLOC_FAILED);
    pte_t *pdpt_id = (pte_t *)(uintptr_t)pdpt_id_phys;
    memset(pdpt_id, 0, PAGE_SIZE);
    pdpt_id[0] = pd0_phys | VMM_PRESENT | VMM_WRITE;

    pml4[0] = pdpt_id_phys | VMM_PRESENT | VMM_WRITE;

    /* ── 3. Higher-half kernel alias ───────────────────────
     *
     * KERNEL_VMA = 0xFFFFFFFF80000000
     *   PML4[511] → PDPT_HH[510] → PD_HH[0] = 2 MiB huge page → phys 0x0
     *
     * One 2 MiB huge page maps 0xFFFFFFFF80000000–0xFFFFFFFF801FFFFF,
     * which covers the entire kernel image (loaded at phys 0x100000).
     */
    uint64_t pdpt_hh_phys = pmm_alloc_frame();
    KASSERT(pdpt_hh_phys != PMM_ALLOC_FAILED);
    pte_t *pdpt_hh = (pte_t *)(uintptr_t)pdpt_hh_phys;
    memset(pdpt_hh, 0, PAGE_SIZE);

    uint64_t pd_hh_phys = pmm_alloc_frame();
    KASSERT(pd_hh_phys != PMM_ALLOC_FAILED);
    pte_t *pd_hh = (pte_t *)(uintptr_t)pd_hh_phys;
    memset(pd_hh, 0, PAGE_SIZE);
    /* Map 0xFFFFFFFF80000000–0xFFFFFFFF801FFFFF → physical 0x0. */
    pd_hh[0] = 0ULL | VMM_PRESENT | VMM_WRITE | VMM_HUGE;

    pdpt_hh[510] = pd_hh_phys | VMM_PRESENT | VMM_WRITE;
    pml4[511]    = pdpt_hh_phys | VMM_PRESENT | VMM_WRITE;

    /* ── 4. Activate ───────────────────────────────────────── */
    kernel_pml4 = pml4;
    vmm_switch(pml4);

    /* ── 5. Init kernel virtual-address allocator ──────────── */
    /*
     * The 2 MiB huge page at pd_hh[0] covers KERNEL_VMA + 0x0 through
     * KERNEL_VMA + 0x1FFFFF.  vmm_map_page refuses to subdivide a huge
     * page, so the heap must start at the NEXT 2 MiB boundary.
     */
    uint64_t kend_hh = KERNEL_VMA + (uint64_t)(uintptr_t)_kernel_end;
    kheap_next = ALIGN_UP(kend_hh, 0x200000ULL);   /* ≥ KERNEL_VMA + 2 MiB */

    kprintf("VMM: new page tables active (CR3 = 0x%lx)\n", pml4_phys);
    kprintf("VMM: null page unmapped (0x0000–0x0FFF)\n");
    kprintf("VMM: guard page unmapped (0x%lx–0x%lx)\n",
            guard_phys, guard_phys + PAGE_SIZE - 1);
    kprintf("VMM: higher-half alias  0x%016lx → phys 0x0\n", KERNEL_VMA);
    kprintf("VMM: kernel virt heap starts at 0x%016lx\n", kheap_next);
}
