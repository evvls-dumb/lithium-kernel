ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

PHDRS {
    text PT_LOAD FLAGS(0x5);
    ro   PT_LOAD FLAGS(0x4);
    data PT_LOAD FLAGS(0x6);
}

SECTIONS {
    . = 0x40000000;

    .text ALIGN(4096) : {
        *(.text .text.*)
    } :text

    .rodata ALIGN(4096) : {
        *(.rodata .rodata.*)
    } :ro

    .data ALIGN(4096) : {
        *(.data .data.*)
    } :data

    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss .bss.*)
    } :data
}
