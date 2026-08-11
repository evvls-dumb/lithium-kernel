bits 64
default rel

global _start
extern main
extern sys_exit

section .text
_start:
    cld
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    lea rdx, [rsi + rdi * 8 + 8]
    call main
    mov rdi, rax
    call sys_exit

.hang:
    jmp .hang
