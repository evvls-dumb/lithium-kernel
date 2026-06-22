/*
 * linker.ld — Lithium kernel linker script
 *
 * Phase 0/1: kernel linked at physical 1 MiB (0x100000).
 * Virtual == physical for now; higher-half remapping is Phase 3.
 *
 * PHDRS block explicitly controls ELF program headers so QEMU's PVH
 * boot loader can find the PT_NOTE segment containing the Xen note.
 * Without an explicit PT_NOTE PHDR, QEMU 9/10 rejects the kernel.
 */

ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

PHDRS {
    pvh_note PT_NOTE  FLAGS(0x4);      /* R       — Xen PVH note for QEMU   */
    rx       PT_LOAD  FLAGS(0x5);      /* R | X   — code + boot headers      */
    ro       PT_LOAD  FLAGS(0x4);      /* R       — read-only data           */
    rw       PT_LOAD  FLAGS(0x6);      /* R | W   — data + BSS               */
}

SECTIONS {
    . = 0x100000;

    /*
     * PVH note — must be in a PT_NOTE segment (QEMU scans PT_NOTE PHDRs).
     * Assign to BOTH :pvh_note and :rx so the bytes are actually loaded
     * into memory by the PT_LOAD segment and visible via PT_NOTE.
     */
    .note.Xen ALIGN(4) : {
        *(.note.Xen)
    } :pvh_note :rx

    /* Multiboot2 header — must appear within first 8 KiB for GRUB. */
    .multiboot2 ALIGN(8) : {
        *(.multiboot2)
    } :rx

    /* Executable code. */
    .text ALIGN(4096) : {
        *(.text .text.*)
    } :rx

    /* Read-only data (string literals, const tables, ISR jump table). */
    .rodata ALIGN(4096) : {
        *(.rodata .rodata.*)
    } :ro

    /* Initialised data. */
    .data ALIGN(4096) : {
        *(.data .data.*)
    } :rw

    /* Zero-initialised data (BSS). */
    .bss ALIGN(4096) : {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    } :rw

    _kernel_end = ALIGN(4096);

    /* Strip noise from the final ELF (but NOT .note.Xen — keep it). */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.GNU-stack)
    }
}
