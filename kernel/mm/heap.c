#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "../../include/kernel.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"
#include "../../kernel/panic.h"

/* ── Size classes ──────────────────────────────────────────────── */
#define SLAB_CLASSES   7
#define SLAB_MIN_SIZE  16UL
#define SLAB_MAX_SIZE  1024UL

static const uint32_t slab_obj_sizes[SLAB_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024
};

/* ── Slab structure (lives at the START of each slab page) ───────
 *
 * Layout per 4 KiB page:
 *   [slab_t  — 64 bytes][object_0][object_1]...[object_N-1]
 *
 * Free objects form an embedded singly-linked list; the first
 * sizeof(void*) bytes of each free slot hold the next-free pointer.
 */
#define SLAB_HDR_SIZE   64   /* keep header a multiple of SLAB_MIN_SIZE */

typedef struct slab {
    uint32_t      magic;       /* SLAB_MAGIC */
    uint32_t      obj_size;    /* size of each object (bytes)            */
    uint16_t      free_count;  /* current number of free objects         */
    uint16_t      total_count; /* total objects in this slab             */
    uint8_t       _pad[4];
    void         *free_list;   /* first free object (or NULL)            */
    struct slab  *next;        /* next slab in the per-class list        */
    struct slab  *prev;
    uint8_t       _reserved[24];
} slab_t;                      /* exactly 64 bytes */

#define SLAB_MAGIC  0xA110C8EDu

/* Per-class linked list of slabs that still have free slots. */
static slab_t *slab_heads[SLAB_CLASSES];

/* Counters for stats. */
static uint64_t heap_alloc_count;
static uint64_t heap_free_count;
static uint64_t heap_slab_pages;
static uint64_t heap_large_pages;

/* ── Internal helpers ────────────────────────────────────────────── */

static int size_to_class(uint32_t sz) {
    for (int i = 0; i < SLAB_CLASSES; i++)
        if (sz <= slab_obj_sizes[i]) return i;
    return -1;
}

/* Allocate a fresh slab page for the given size class. */
static slab_t *slab_alloc_page(int cls) {
    uint64_t phys = pmm_alloc_frame();
    if (phys == PMM_ALLOC_FAILED) return NULL;

    /* Map it into virtual kernel heap space. */
    uint64_t virt = vmm_alloc_kernel(PAGE_SIZE);
    if (vmm_map_page(vmm_current_pml4(), virt, phys, VMM_WRITE) != 0) {
        pmm_free_frame(phys);
        return NULL;
    }

    slab_t *s = (slab_t *)(uintptr_t)virt;
    uint32_t obj_size  = slab_obj_sizes[cls];
    uint32_t total     = (PAGE_SIZE - SLAB_HDR_SIZE) / obj_size;

    s->magic       = SLAB_MAGIC;
    s->obj_size    = obj_size;
    s->free_count  = (uint16_t)total;
    s->total_count = (uint16_t)total;
    s->next        = NULL;
    s->prev        = NULL;

    /* Build the embedded free-list through the objects. */
    uint8_t *base = (uint8_t *)s + SLAB_HDR_SIZE;
    s->free_list  = base;
    for (uint32_t i = 0; i < total - 1; i++) {
        void **slot = (void **)(base + i * obj_size);
        *slot = base + (i + 1) * obj_size;
    }
    /* Last slot → NULL */
    *(void **)(base + (total - 1) * obj_size) = NULL;

    heap_slab_pages++;
    return s;
}

/* Given a pointer, find its slab header (at page base). */
static inline slab_t *slab_of(void *ptr) {
    return (slab_t *)((uintptr_t)ptr & ~(PAGE_SIZE - 1));
}

/* ── kmalloc / kfree ─────────────────────────────────────────────── */

void heap_init(void) {
    memset(slab_heads, 0, sizeof(slab_heads));
    heap_alloc_count = heap_free_count = 0;
    heap_slab_pages  = heap_large_pages = 0;
    kprintf("heap: slab allocator ready (%d size classes, %u–%u bytes)\n",
            SLAB_CLASSES, (unsigned)SLAB_MIN_SIZE, (unsigned)SLAB_MAX_SIZE);
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Round up to minimum alignment. */
    if (size < SLAB_MIN_SIZE) size = SLAB_MIN_SIZE;

    int cls = size_to_class((uint32_t)size);

    if (cls >= 0) {
        /* ── Slab path ────────────────────────────────────── */
        slab_t *s = slab_heads[cls];

        /* Find a slab with free space, or create one. */
        while (s && s->free_count == 0) s = s->next;

        if (!s) {
            s = slab_alloc_page(cls);
            if (!s) return NULL;
            /* Prepend to list. */
            s->next = slab_heads[cls];
            if (slab_heads[cls]) slab_heads[cls]->prev = s;
            slab_heads[cls] = s;
        }

        void *obj = s->free_list;
        s->free_list = *(void **)obj;
        s->free_count--;
        heap_alloc_count++;
        return obj;
    } else {
        /* ── Large path (> 1024 bytes) ───────────────────── */
        size_t total    = size + 16;           /* 16-byte header */
        size_t pages    = ALIGN_UP(total, PAGE_SIZE) / PAGE_SIZE;
        uint64_t virt   = vmm_alloc_kernel(pages * PAGE_SIZE);

        for (size_t i = 0; i < pages; i++) {
            uint64_t phys = pmm_alloc_frame();
            if (phys == PMM_ALLOC_FAILED) {
                /* TODO: roll back already-mapped pages */
                return NULL;
            }
            if (vmm_map_page(vmm_current_pml4(),
                             virt + i * PAGE_SIZE, phys, VMM_WRITE) != 0) {
                pmm_free_frame(phys);
                return NULL;
            }
        }

        uint64_t *hdr = (uint64_t *)(uintptr_t)virt;
        hdr[0] = (uint64_t)pages;
        hdr[1] = 0xFEEDFACEDEADBEEFULL;   /* canary */

        heap_large_pages += pages;
        heap_alloc_count++;
        return (void *)(uintptr_t)(virt + 16);
    }
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;

    /* Determine whether this is a slab or large allocation. */
    slab_t *s = slab_of(ptr);

    if (s->magic == SLAB_MAGIC) {
        /* ── Slab path ────────────────────────────────────── */
        KASSERT((uintptr_t)ptr >= (uintptr_t)s + SLAB_HDR_SIZE);

        *(void **)ptr = s->free_list;
        s->free_list  = ptr;
        s->free_count++;
        heap_free_count++;
    } else {
        /* ── Large path ───────────────────────────────────── */
        uint64_t *hdr = (uint64_t *)((uintptr_t)ptr - 16);
        KASSERT(hdr[1] == 0xFEEDFACEDEADBEEFULL);

        uint64_t pages = hdr[0];
        uint64_t virt  = (uint64_t)(uintptr_t)hdr;

        for (uint64_t i = 0; i < pages; i++) {
            uint64_t phys = vmm_get_physical(vmm_current_pml4(),
                                             virt + i * PAGE_SIZE);
            if (phys != PMM_ALLOC_FAILED) pmm_free_frame(phys);
            vmm_unmap_page(vmm_current_pml4(), virt + i * PAGE_SIZE);
        }

        heap_large_pages -= pages;
        heap_free_count++;
    }
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr)       return kmalloc(new_size);
    if (!new_size)  { kfree(ptr); return NULL; }

    /* Determine old size from slab or large header. */
    slab_t *s = slab_of(ptr);
    size_t old_size;
    if (s->magic == SLAB_MAGIC)
        old_size = s->obj_size;
    else
        old_size = ((uint64_t *)((uintptr_t)ptr - 16))[0] * PAGE_SIZE - 16;

    if (new_size <= old_size) return ptr;

    void *np = kmalloc(new_size);
    if (!np) return NULL;
    memcpy(np, ptr, old_size);
    kfree(ptr);
    return np;
}

void heap_stats(void) {
    kprintf("heap: allocs=%lu  frees=%lu  slab_pages=%lu  large_pages=%lu\n",
            heap_alloc_count, heap_free_count,
            heap_slab_pages, heap_large_pages);
}
