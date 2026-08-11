#pragma once

#include "../../include/types.h"
#include "../mm/vmm.h"

typedef struct {
    uint64_t entry;
    uint64_t brk_base;
} elf_user_image_t;

int elf_load_user_image(pte_t *pml4,
                        const void *image,
                        size_t image_size,
                        elf_user_image_t *out);
