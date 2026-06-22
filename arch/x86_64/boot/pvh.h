#pragma once

#include "../../../include/types.h"

/*
 * Xen / QEMU PVH boot protocol structures.
 *
 * When QEMU boots our kernel via the PVH ELF note:
 *   EBX = physical address of hvm_start_info_t
 *   EAX = undefined (QEMU does not set XEN_HVM_START_MAGIC_VALUE in EAX)
 *
 * The hvm_start_info struct is in low physical memory (<1 MiB) so it is
 * directly dereferenceable via our identity mapping.
 */

#define HVM_START_MAGIC     0x336ec578u   /* magic in hvm_start_info.magic */

/* E820-compatible memory types used in hvm_memmap_table_entry.type */
#define HVM_MMAP_RAM        1   /* Usable RAM                            */
#define HVM_MMAP_RESERVED   2   /* Reserved / do not use                 */
#define HVM_MMAP_ACPI       3   /* ACPI reclaimable                      */
#define HVM_MMAP_NVS        4   /* ACPI NVS                              */
#define HVM_MMAP_UNUSABLE   5   /* Bad RAM                               */

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* 0x336ec578                            */
    uint32_t version;           /* Version of this structure             */
    uint32_t flags;
    uint32_t nr_modules;
    uint64_t modlist_paddr;
    uint64_t cmdline_paddr;
    uint64_t rsdp_paddr;        /* Physical address of ACPI RSDP         */
    uint64_t memmap_paddr;      /* Physical address of memmap table      */
    uint32_t memmap_entries;    /* Number of entries in the table        */
    uint32_t reserved;
} hvm_start_info_t;

/* Each entry is an E820-style memory descriptor. */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
    uint32_t reserved;
} hvm_memmap_entry_t;
