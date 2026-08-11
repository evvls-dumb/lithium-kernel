bits 64

global user_sh_elf_start
global user_sh_elf_end

section .rodata
align 16
user_sh_elf_start:
    incbin "user/sh.elf"
user_sh_elf_end:
