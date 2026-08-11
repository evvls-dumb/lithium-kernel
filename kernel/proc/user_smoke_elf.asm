bits 64

global user_smoke_elf_start
global user_smoke_elf_end

section .rodata
align 16
user_smoke_elf_start:
    incbin "user/smoke.elf"
user_smoke_elf_end:
