bits 64

global user_init_elf_start
global user_init_elf_end

section .rodata
align 16
user_init_elf_start:
    incbin "user/init.elf"
user_init_elf_end:
