#include "pmm.h"
#include "../../include/multiboot2.h"
#include "../../arch/x86_64/boot/pvh.h"
#include "../../include/kernel.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"
#include "../../kernel/panic.h"

/* ── Bitmap storage ──────────────────────────────────────────────
 * 128 KiB in .bss — zero-initialised at boot.
 * Bit N set (1) means frame N is in use; clear (0) means free.
 */
static uint8_t  pmm_bitmap[PMM_BITMAP_BYTES] __attribute__((aligned(8)));
static uint16_t pmm_refcount[PMM_MAX_FRAMES];
static uint64_t pmm_total_frames = 0;  /* highest usable frame index + 1 */
static uint64_t pmm_free_frames  = 0;
static uint64_t pmm_next_hint    = 0;

/* Kernel image boundaries supplied by the linker script. */
extern char _kernel_end[];

/* ── Bit-level helpers ───────────────────────────────────────── */

static inline void frame_set_used(uint64_t f) {
    pmm_bitmap[f >> 3] |= (uint8_t)(1u << (f & 7));
}

static inline void frame_set_free(uint64_t f) {
    pmm_bitmap[f >> 3] &= (uint8_t)~(1u << (f & 7));
}

static inline bool frame_is_free(uint64_t f) {
    return !(pmm_bitmap[f >> 3] & (uint8_t)(1u << (f & 7)));
}

/* ── Region helpers ──────────────────────────────────────────── */

/* Mark [base, base+len) as free; clamps to known frames. */
static void region_mark_free(uint64_t base, uint64_t len) {
    uint64_t first = base / PMM_FRAME_SIZE;
    uint64_t last  = (base + len) / PMM_FRAME_SIZE;  /* exclusive */
    for (uint64_t f = first; f < last && f < pmm_total_frames; f++) {
        if (!frame_is_free(f)) {
            frame_set_free(f);
            pmm_refcount[f] = 0;
            pmm_free_frames++;
        }
    }
}

/* Mark [base, base+len) as used; clamps to known frames. */
static void region_mark_used(uint64_t base, uint64_t len) {
    uint64_t first = base / PMM_FRAME_SIZE;
    uint64_t last  = ALIGN_UP(base + len, PMM_FRAME_SIZE) / PMM_FRAME_SIZE;
    for (uint64_t f = first; f < last && f < pmm_total_frames; f++) {
        if (frame_is_free(f)) {
            frame_set_used(f);
            pmm_refcount[f] = 1;
            pmm_free_frames--;
        }
    }
}

/* ── Memory-map parsers ──────────────────────────────────────── */

/*
 * First pass: determine pmm_total_frames from the highest usable address.
 * Reserved MMIO holes can sit near 4 GiB; counting them would make every
 * allocation scan far beyond the RAM the VM actually owns.
 */
static void pmm_calc_total_frames_pvh(const hvm_memmap_entry_t *mmap,
                                      uint32_t count) {
    uint64_t top = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (mmap[i].type != HVM_MMAP_RAM)
            continue;
        uint64_t end = mmap[i].addr + mmap[i].size;
        if (end > top) top = end;
    }
    /* Clamp to the maximum the bitmap can represent. */
    if (top > PMM_MAX_PHYS) top = PMM_MAX_PHYS;
    pmm_total_frames = top / PMM_FRAME_SIZE;
}

static void pmm_calc_total_frames_mb2(const mb2_tag_mmap_t *tag) {
    uint64_t top = 0;
    const uint8_t *p   = (const uint8_t *)(tag + 1);
    const uint8_t *end = (const uint8_t *)tag + tag->size;
    while (p + tag->entry_size <= end) {
        const mb2_mmap_entry_t *e = (const mb2_mmap_entry_t *)p;
        if (e->type != MB2_MMAP_AVAILABLE) {
            p += tag->entry_size;
            continue;
        }
        uint64_t eend = e->base_addr + e->length;
        if (eend > top) top = eend;
        p += tag->entry_size;
    }
    if (top > PMM_MAX_PHYS) top = PMM_MAX_PHYS;
    pmm_total_frames = top / PMM_FRAME_SIZE;
}

/* Parse PVH (Xen/QEMU) E820-style memory map. */
static void pmm_parse_pvh(uint32_t info_phys) {
    const hvm_start_info_t *si =
        (const hvm_start_info_t *)(uintptr_t)info_phys;

    if (si->magic != HVM_START_MAGIC) {
        panic("PMM: PVH start_info magic mismatch");
    }

    uint32_t count = si->memmap_entries;
    const hvm_memmap_entry_t *mmap =
        (const hvm_memmap_entry_t *)(uintptr_t)si->memmap_paddr;

    kprintf("PMM: PVH memory map (%u entries):\n", count);

    /* Determine total frame count first. */
    pmm_calc_total_frames_pvh(mmap, count);

    /* Start with all frames marked used. */
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    memset(pmm_refcount, 0, sizeof(pmm_refcount));
    pmm_free_frames = 0;

    /* Iterate map: mark RAM entries free. */
    for (uint32_t i = 0; i < count; i++) {
        const char *tname = "reserved";
        if (mmap[i].type == HVM_MMAP_RAM)       tname = "usable";
        else if (mmap[i].type == HVM_MMAP_ACPI)  tname = "ACPI";

        kprintf("  [%u] 0x%016lx + 0x%lx  %s\n",
                i, mmap[i].addr, mmap[i].size, tname);

        if (mmap[i].type == HVM_MMAP_RAM) {
            region_mark_free(mmap[i].addr, mmap[i].size);
        }
    }
}

/* Parse Multiboot2 info structure, find tag type=6 (mmap). */
static void pmm_parse_mb2(uint32_t info_phys) {
    const mb2_info_t *info = (const mb2_info_t *)(uintptr_t)info_phys;
    const mb2_tag_mmap_t *mmap_tag = NULL;

    const uint8_t *p = (const uint8_t *)(info + 1);
    const uint8_t *end = (const uint8_t *)info + info->total_size;

    while (p < end) {
        const mb2_tag_t *tag = (const mb2_tag_t *)p;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_MMAP) {
            mmap_tag = (const mb2_tag_mmap_t *)tag;
            break;
        }
        /* Tags are 8-byte aligned. */
        p += ALIGN_UP(tag->size, 8);
    }

    if (!mmap_tag) panic("PMM: no Multiboot2 memory-map tag found");

    pmm_calc_total_frames_mb2(mmap_tag);
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    memset(pmm_refcount, 0, sizeof(pmm_refcount));
    pmm_free_frames = 0;

    kprintf("PMM: Multiboot2 memory map:\n");

    const uint8_t *ep   = (const uint8_t *)(mmap_tag + 1);
    const uint8_t *eend = (const uint8_t *)mmap_tag + mmap_tag->size;
    uint32_t idx = 0;
    while (ep + mmap_tag->entry_size <= eend) {
        const mb2_mmap_entry_t *e = (const mb2_mmap_entry_t *)ep;
        const char *tname = "reserved";
        if (e->type == MB2_MMAP_AVAILABLE) tname = "usable";
        else if (e->type == MB2_MMAP_ACPI) tname = "ACPI";

        kprintf("  [%u] 0x%016lx + 0x%lx  %s\n",
                idx++, e->base_addr, e->length, tname);

        if (e->type == MB2_MMAP_AVAILABLE) {
            region_mark_free(e->base_addr, e->length);
        }
        ep += mmap_tag->entry_size;
    }
}

/* ── Public API ──────────────────────────────────────────────── */

void pmm_init(uint32_t boot_magic, uint32_t boot_info_phys) {
    /*
     * Choose parser based on boot protocol.
     * On PVH boot EAX is not set by QEMU, so we detect PVH by reading
     * the magic field in the hvm_start_info struct at boot_info_phys.
     */
    if (boot_magic == MB2_BOOTLOADER_MAGIC) {
        kprintf("PMM: parsing Multiboot2 memory map\n");
        pmm_parse_mb2(boot_info_phys);
    } else {
        /* PVH or unknown — try reading hvm_start_info. */
        const hvm_start_info_t *si =
            (const hvm_start_info_t *)(uintptr_t)boot_info_phys;
        if (boot_info_phys != 0 && si->magic == HVM_START_MAGIC) {
            kprintf("PMM: parsing PVH/QEMU memory map\n");
            pmm_parse_pvh(boot_info_phys);
        } else {
            panic("PMM: cannot locate a memory map — unsupported boot path");
        }
    }

    KASSERT(pmm_total_frames > 0);
    pmm_next_hint = 0;

    /*
     * Re-mark the bottom 1 MiB as used.
     * Covers: real-mode IVT, BIOS data area, VGA frame buffer (0xA0000),
     * BIOS ROM (0xC0000-0xFFFFF).  We never hand these frames out.
     */
    region_mark_used(0, 0x100000);

    /*
     * Re-mark the kernel image (text + rodata + data + bss, including
     * the PMM bitmap itself) as used.  _kernel_end comes from linker.ld.
     */
    uint64_t kernel_end = (uint64_t)(uintptr_t)_kernel_end;
    region_mark_used(0x100000, kernel_end - 0x100000);

    kprintf("PMM: total frames : %lu  (%lu MiB)\n",
            pmm_total_frames,
            pmm_total_frames * PMM_FRAME_SIZE / (1024 * 1024));
    kprintf("PMM: free  frames : %lu  (%lu MiB)\n",
            pmm_free_frames,
            pmm_free_frames  * PMM_FRAME_SIZE / (1024 * 1024));
    kprintf("PMM: kernel image : 0x%016lx – 0x%016lx\n",
            (uint64_t)0x100000, kernel_end);
}

uint64_t pmm_alloc_frame(void) {
    if (pmm_free_frames == 0) return PMM_ALLOC_FAILED;

    uint64_t n_bytes = (pmm_total_frames + 7) / 8;
    uint64_t start_byte = pmm_next_hint >> 3;

    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t byte_begin = pass == 0 ? start_byte : 0;
        uint64_t byte_end   = pass == 0 ? n_bytes : start_byte;
        for (uint64_t byte = byte_begin; byte < byte_end; byte++) {
            if (pmm_bitmap[byte] == 0xFF) continue;   /* all 8 frames used */
            for (int bit = 0; bit < 8; bit++) {
                uint64_t f = byte * 8 + (uint64_t)bit;
                if (f >= pmm_total_frames) break;
                if (!(pmm_bitmap[byte] & (uint8_t)(1u << bit))) {
                    pmm_bitmap[byte] |= (uint8_t)(1u << bit);
                    pmm_refcount[f] = 1;
                    pmm_free_frames--;
                    pmm_next_hint = f + 1;
                    return f * PMM_FRAME_SIZE;
                }
            }
        }
    }
    return PMM_ALLOC_FAILED;
}

void pmm_free_frame(uint64_t phys_addr) {
    KASSERT((phys_addr & (PMM_FRAME_SIZE - 1)) == 0);  /* must be aligned */
    uint64_t f = phys_addr / PMM_FRAME_SIZE;
    KASSERT(f < pmm_total_frames);
    KASSERT(!frame_is_free(f));   /* double-free check */
    KASSERT(pmm_refcount[f] > 0);
    pmm_refcount[f]--;
    if (pmm_refcount[f] > 0) return;
    frame_set_free(f);
    if (f < pmm_next_hint) pmm_next_hint = f;
    pmm_free_frames++;
}

void pmm_ref_frame(uint64_t phys_addr) {
    KASSERT((phys_addr & (PMM_FRAME_SIZE - 1)) == 0);
    uint64_t f = phys_addr / PMM_FRAME_SIZE;
    KASSERT(f < pmm_total_frames);
    KASSERT(!frame_is_free(f));
    KASSERT(pmm_refcount[f] > 0);
    KASSERT(pmm_refcount[f] < UINT16_MAX);
    pmm_refcount[f]++;
}

uint32_t pmm_frame_refcount(uint64_t phys_addr) {
    KASSERT((phys_addr & (PMM_FRAME_SIZE - 1)) == 0);
    uint64_t f = phys_addr / PMM_FRAME_SIZE;
    KASSERT(f < pmm_total_frames);
    return pmm_refcount[f];
}

uint64_t pmm_alloc_contiguous(uint32_t count) {
    if (count == 0 || pmm_free_frames < count) return PMM_ALLOC_FAILED;

    uint64_t run_start = 0;
    uint32_t run_len   = 0;

    for (uint64_t f = 0; f < pmm_total_frames; f++) {
        if (frame_is_free(f)) {
            if (run_len == 0) run_start = f;
            run_len++;
            if (run_len == count) {
                /* Found a run — mark all frames used. */
                for (uint64_t j = run_start; j < run_start + count; j++) {
                    frame_set_used(j);
                    pmm_refcount[j] = 1;
                }
                pmm_free_frames -= count;
                return run_start * PMM_FRAME_SIZE;
            }
        } else {
            run_len = 0;
        }
    }
    return PMM_ALLOC_FAILED;
}

uint64_t pmm_total_bytes(void) {
    return pmm_total_frames * PMM_FRAME_SIZE;
}

uint64_t pmm_free_bytes(void) {
    return pmm_free_frames * PMM_FRAME_SIZE;
}

void pmm_stats(void) {
    uint64_t used = pmm_total_frames - pmm_free_frames;
    kprintf("PMM stats:\n");
    kprintf("  Total : %6lu MiB  (%lu frames)\n",
            pmm_total_bytes() / (1024*1024), pmm_total_frames);
    kprintf("  Free  : %6lu MiB  (%lu frames)\n",
            pmm_free_bytes()  / (1024*1024), pmm_free_frames);
    kprintf("  Used  : %6lu MiB  (%lu frames)\n",
            used * PMM_FRAME_SIZE / (1024*1024), used);
}

void pmm_selftest(void) {
    kprintf("[  ] PMM self-test: 1000-frame alloc/free cycle...\n");

    /* Store addresses in a static array (avoids consuming 8 KiB of stack). */
    static uint64_t frames[1000];

    uint64_t free_before = pmm_free_frames;

    /* Allocate 1000 frames. */
    for (int i = 0; i < 1000; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PMM_ALLOC_FAILED) {
            kprintf("[!!] PMM self-test: allocation #%d failed!\n", i);
            panic("PMM self-test allocation failure");
        }
        /* Every frame must be 4 KiB aligned and non-zero (never alloc frame 0). */
        KASSERT((frames[i] & (PMM_FRAME_SIZE - 1)) == 0);
        KASSERT(frames[i] != 0);
    }

    KASSERT(pmm_free_frames == free_before - 1000);
    kprintf("[OK] Allocated 1000 frames (free: %lu → %lu)\n",
            free_before, pmm_free_frames);

    /* Verify no duplicates (spot-check: each address must still be marked used). */
    for (int i = 0; i < 1000; i++) {
        uint64_t f = frames[i] / PMM_FRAME_SIZE;
        KASSERT(!frame_is_free(f));  /* must still be marked used */
    }

    /* Free all 1000 frames. */
    for (int i = 0; i < 1000; i++) {
        pmm_free_frame(frames[i]);
    }

    KASSERT(pmm_free_frames == free_before);
    kprintf("[OK] Freed  1000 frames  (free restored to %lu)\n",
            pmm_free_frames);

    /* Contiguous allocation test — 16 physically contiguous frames (64 KiB). */
    uint64_t contiguous = pmm_alloc_contiguous(16);
    KASSERT(contiguous != PMM_ALLOC_FAILED);
    KASSERT((contiguous & (PMM_FRAME_SIZE - 1)) == 0);
    /* Verify they are genuinely contiguous by freeing and re-checking. */
    for (int i = 0; i < 16; i++) {
        pmm_free_frame(contiguous + (uint64_t)i * PMM_FRAME_SIZE);
    }

    KASSERT(pmm_free_frames == free_before);
    kprintf("[OK] Contiguous alloc/free 16 frames @ 0x%016lx\n", contiguous);
    kprintf("[OK] PMM self-test PASSED\n");
}
