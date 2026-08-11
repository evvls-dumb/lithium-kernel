bits 64

global user_execer_elf_start
global user_execer_elf_end

section .rodata
align 16
user_execer_elf_start:
    incbin "user/execer.elf"
user_execer_elf_end:
