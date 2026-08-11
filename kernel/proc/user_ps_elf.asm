bits 64

global user_ps_elf_start
global user_ps_elf_end

section .rodata
align 16
user_ps_elf_start:
    incbin "user/ps.elf"
user_ps_elf_end:
