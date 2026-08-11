bits 64
default rel

global _start

section .text
_start:
    cld

    mov eax, 1
    mov edi, 1
    lea rsi, [start_msg]
    mov edx, start_msg_end - start_msg
    syscall
    cmp rax, start_msg_end - start_msg
    jne .fail

    mov eax, 59
    lea rdi, [init_path]
    lea rsi, [argv_vec]
    lea rdx, [envp_vec]
    syscall

.fail:
    mov eax, 1
    mov edi, 2
    lea rsi, [fail_msg]
    mov edx, fail_msg_end - fail_msg
    syscall

    mov eax, 60
    mov edi, 1
    syscall

.hang:
    jmp .hang

section .rodata
start_msg:
    db "[user] handoff execve /bin/init", 10
start_msg_end:
fail_msg:
    db "[user] init handoff failed", 10
fail_msg_end:
init_path:
    db "/bin/init", 0
arg_magic:
    db "boot", 0
env_magic:
    db "INIT=1", 0

align 8
argv_vec:
    dq init_path
    dq arg_magic
    dq 0
envp_vec:
    dq env_magic
    dq 0
