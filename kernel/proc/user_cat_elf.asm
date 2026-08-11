bits 64

global user_cat_elf_start
global user_cat_elf_end

section .rodata
align 16
user_cat_elf_start:
    incbin "user/cat.elf"
user_cat_elf_end:
