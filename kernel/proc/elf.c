#include "elf.h"
#include "../../include/kernel.h"
#include "../../kernel/mm/pmm.h"
#include "../../kernel/mm/vmm.h"
#include "../../lib/string.h"

#define EI_NIDENT 16

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_X86_64 62

#define PT_LOAD 1

#define PF_X 1
#define PF_W 2

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

static bool range_ok(uint64_t start, uint64_t size, uint64_t limit) {
    return size <= limit && start <= limit - size;
}

static int map_segment_page(pte_t *pml4,
                            uint64_t virt,
                            const uint8_t *file,
                            size_t file_size,
                            const elf64_phdr_t *ph) {
    uint64_t phys = pmm_alloc_frame();
    if (phys == PMM_ALLOC_FAILED) return -1;

    uint8_t *dst = (uint8_t *)(uintptr_t)phys;
    memset(dst, 0, PAGE_SIZE);

    uint64_t page_start = ALIGN_DOWN(virt, PAGE_SIZE);
    uint64_t page_end = page_start + PAGE_SIZE;
    uint64_t seg_file_start = ph->p_vaddr;
    uint64_t seg_file_end = ph->p_vaddr + ph->p_filesz;

    uint64_t copy_start = MAX(page_start, seg_file_start);
    uint64_t copy_end = MIN(page_end, seg_file_end);
    if (copy_start < copy_end) {
        uint64_t file_off = ph->p_offset + (copy_start - ph->p_vaddr);
        if (!range_ok(file_off, copy_end - copy_start, file_size)) {
            pmm_free_frame(phys);
            return -1;
        }
        memcpy(dst + (copy_start - page_start),
               file + file_off,
               (size_t)(copy_end - copy_start));
    }

    uint64_t flags = VMM_USER;
    if (ph->p_flags & PF_W) flags |= VMM_WRITE;
    if (!(ph->p_flags & PF_X)) flags |= VMM_NX;

    if (vmm_map_page(pml4, page_start, phys, flags) != 0) {
        pmm_free_frame(phys);
        return -1;
    }

    return 0;
}

int elf_load_user_image(pte_t *pml4,
                        const void *image,
                        size_t image_size,
                        elf_user_image_t *out) {
    if (!pml4 || !image || !out || image_size < sizeof(elf64_ehdr_t))
        return -1;

    const uint8_t *file = (const uint8_t *)image;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)file;

    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3)
        return -1;
    if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB)
        return -1;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64 ||
        eh->e_version != EV_CURRENT)
        return -1;
    if (eh->e_ehsize != sizeof(elf64_ehdr_t) ||
        eh->e_phentsize != sizeof(elf64_phdr_t) ||
        eh->e_phnum == 0 || eh->e_phnum > 32)
        return -1;
    if (!range_ok(eh->e_phoff, (uint64_t)eh->e_phnum * eh->e_phentsize, image_size))
        return -1;
    if (eh->e_entry == 0 || eh->e_entry >= USER_SPACE_TOP)
        return -1;

    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)(file + eh->e_phoff);
    uint64_t highest = 0;
    bool saw_load = false;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_memsz < ph->p_filesz || ph->p_memsz == 0)
            return -1;
        if (!range_ok(ph->p_offset, ph->p_filesz, image_size))
            return -1;
        if (!range_ok(ph->p_vaddr, ph->p_memsz, USER_SPACE_TOP))
            return -1;
        if (ph->p_align && ph->p_align != PAGE_SIZE)
            return -1;

        uint64_t first = ALIGN_DOWN(ph->p_vaddr, PAGE_SIZE);
        uint64_t last = ALIGN_UP(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        for (uint64_t page = first; page < last; page += PAGE_SIZE) {
            if (map_segment_page(pml4, page, file, image_size, ph) != 0)
                return -1;
        }

        if (ph->p_vaddr + ph->p_memsz > highest)
            highest = ph->p_vaddr + ph->p_memsz;
        saw_load = true;
    }

    if (!saw_load)
        return -1;

    out->entry = eh->e_entry;
    out->brk_base = ALIGN_UP(highest, PAGE_SIZE);
    return 0;
}
