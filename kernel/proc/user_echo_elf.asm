bits 64

global user_echo_elf_start
global user_echo_elf_end

section .rodata
align 16
user_echo_elf_start:
    incbin "user/echo.elf"
user_echo_elf_end:
