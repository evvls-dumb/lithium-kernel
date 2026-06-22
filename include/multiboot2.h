#pragma once

#include "types.h"

/*
 * Multiboot2 info-structure types used by the PMM.
 * Reference: https://www.gnu.org/software/grub/manual/multiboot2/
 */

#define MB2_BOOTLOADER_MAGIC  0x36D76289u

/* Fixed header at the base of the Multiboot2 info structure. */
typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t reserved;
} mb2_info_t;

/* Every tag starts with type + size. */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

#define MB2_TAG_END   0
#define MB2_TAG_MMAP  6

/* Memory-map tag header (type=6). */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} mb2_tag_mmap_t;

/* One memory-map entry. */
typedef struct __attribute__((packed)) {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} mb2_mmap_entry_t;

/* Memory-map entry types. */
#define MB2_MMAP_AVAILABLE  1   /* Usable RAM          */
#define MB2_MMAP_RESERVED   2
#define MB2_MMAP_ACPI       3   /* ACPI reclaimable    */
#define MB2_MMAP_NVS        4   /* ACPI NVS            */
#define MB2_MMAP_BADRAM     5
