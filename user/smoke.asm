bits 64
default rel

global _start

section .text
_start:
    cld

    cmp qword [rsp], 2
    jne .fail_argv
    cmp qword [rsp + 24], 0
    jne .fail_argv
    cmp qword [rsp + 40], 0
    jne .fail_argv

    mov rdi, [rsp + 16]
    lea rsi, [arg_expected]
    call check_string
    test eax, eax
    jne .fail_argv

    mov rdi, [rsp + 32]
    lea rsi, [env_expected]
    call check_string
    test eax, eax
    jne .fail_argv

    mov eax, 1
    mov edi, 1
    lea rsi, [argv_msg]
    mov edx, argv_msg_end - argv_msg
    syscall
    cmp rax, argv_msg_end - argv_msg
    jne .fail

    mov eax, 1
    mov edi, 1
    lea rsi, [smoke_msg]
    mov edx, smoke_msg_end - smoke_msg
    syscall
    cmp rax, smoke_msg_end - smoke_msg
    jne .fail

    mov eax, 39
    syscall
    test rax, rax
    jle .fail

    mov eax, 1
    mov edi, 1
    mov esi, 0x70000000
    mov edx, 4
    syscall
    cmp rax, -14
    jne .fail_badptr

    mov eax, 2
    mov edi, 0x70000000
    syscall
    cmp rax, -14
    jne .fail_badptr

    mov eax, 1
    mov edi, 1
    lea rsi, [badptr_msg]
    mov edx, badptr_msg_end - badptr_msg
    syscall
    cmp rax, badptr_msg_end - badptr_msg
    jne .fail

    mov eax, 12
    xor edi, edi
    syscall
    test rax, rax
    jle .fail
    mov rbx, rax

    mov rdi, rbx
    add rdi, 4096
    mov eax, 12
    syscall
    cmp rax, rdi
    jne .fail

    mov rdi, rbx
    lea rsi, [brk_msg]
    mov ecx, brk_msg_end - brk_msg
    rep movsb

    mov eax, 1
    mov edi, 1
    mov rsi, rbx
    mov edx, brk_msg_end - brk_msg
    syscall
    cmp rax, brk_msg_end - brk_msg
    jne .fail
    mov r12, rbx

    mov eax, 9
    xor edi, edi
    mov esi, 4096
    mov edx, 3
    mov r10d, 0x22
    mov r8, -1
    xor r9d, r9d
    syscall
    test rax, rax
    js .fail_mmap_sys
    mov rbx, rax

    mov rdi, rbx
    lea rsi, [mmap_msg]
    mov ecx, mmap_msg_end - mmap_msg
    rep movsb

    mov eax, 1
    mov edi, 1
    mov rsi, rbx
    mov edx, mmap_msg_end - mmap_msg
    syscall
    cmp rax, mmap_msg_end - mmap_msg
    jne .fail_mmap_write

    mov eax, 11
    mov rdi, rbx
    mov esi, 4096
    syscall
    test rax, rax
    jne .fail_munmap

    mov eax, 61
    mov rdi, -1
    xor esi, esi
    mov edx, 1
    syscall
    cmp rax, -10
    jne .fail_waitpid

    mov eax, 1
    mov edi, 1
    lea rsi, [waitpid_msg]
    mov edx, waitpid_msg_end - waitpid_msg
    syscall
    cmp rax, waitpid_msg_end - waitpid_msg
    jne .fail

    mov rax, 0x1111111122222222
    mov [r12 + 8], rax
    mov eax, 57
    syscall
    test rax, rax
    js .fail_fork
    jz .fork_child

    mov r13, rax
    mov qword [r12], 0
    mov eax, 61
    mov rdi, r13
    mov rsi, r12
    xor edx, edx
    syscall
    cmp rax, r13
    jne .fail_waitpid_child
    cmp dword [r12], 0x700
    jne .fail_waitpid_child
    mov rax, 0x1111111122222222
    cmp qword [r12 + 8], rax
    jne .fail_cow

    mov eax, 1
    mov edi, 1
    lea rsi, [fork_msg]
    mov edx, fork_msg_end - fork_msg
    syscall
    cmp rax, fork_msg_end - fork_msg
    jne .fail

    mov eax, 60
    xor edi, edi
    syscall

.fork_child:
    mov rax, 0x3333333344444444
    mov [r12 + 8], rax

    mov eax, 1
    mov edi, 1
    lea rsi, [fork_child_msg]
    mov edx, fork_child_msg_end - fork_child_msg
    syscall
    cmp rax, fork_child_msg_end - fork_child_msg
    jne .fail

    mov eax, 60
    mov edi, 7
    syscall

.fail_argv:
    lea rsi, [fail_argv_msg]
    mov edx, fail_argv_msg_end - fail_argv_msg
    jmp .fail_print

.fail_mmap_sys:
    lea rsi, [fail_mmap_sys_msg]
    mov edx, fail_mmap_sys_msg_end - fail_mmap_sys_msg
    jmp .fail_print

.fail_mmap_write:
    lea rsi, [fail_mmap_write_msg]
    mov edx, fail_mmap_write_msg_end - fail_mmap_write_msg
    jmp .fail_print

.fail_munmap:
    lea rsi, [fail_munmap_msg]
    mov edx, fail_munmap_msg_end - fail_munmap_msg
    jmp .fail_print

.fail_waitpid:
    lea rsi, [fail_waitpid_msg]
    mov edx, fail_waitpid_msg_end - fail_waitpid_msg
    jmp .fail_print

.fail_badptr:
    lea rsi, [fail_badptr_msg]
    mov edx, fail_badptr_msg_end - fail_badptr_msg
    jmp .fail_print

.fail_fork:
    lea rsi, [fail_fork_msg]
    mov edx, fail_fork_msg_end - fail_fork_msg
    jmp .fail_print

.fail_waitpid_child:
    lea rsi, [fail_waitpid_child_msg]
    mov edx, fail_waitpid_child_msg_end - fail_waitpid_child_msg
    jmp .fail_print

.fail_cow:
    lea rsi, [fail_cow_msg]
    mov edx, fail_cow_msg_end - fail_cow_msg
    jmp .fail_print

.fail:
    lea rsi, [fail_msg]
    mov edx, fail_msg_end - fail_msg

.fail_print:
    mov eax, 1
    mov edi, 2
    syscall

    mov eax, 60
    mov edi, 1
    syscall

.hang:
    jmp .hang

check_string:
    mov al, [rdi]
    cmp al, [rsi]
    jne .mismatch
    test al, al
    je .match
    inc rdi
    inc rsi
    jmp check_string
.match:
    xor eax, eax
    ret
.mismatch:
    mov eax, 1
    ret

section .rodata
argv_msg:
    db "[user] exec argv/envp OK", 10
argv_msg_end:
smoke_msg:
    db "[user] syscall write smoke OK", 10
smoke_msg_end:
badptr_msg:
    db "[user] bad user pointer smoke OK", 10
badptr_msg_end:
brk_msg:
    db "[user] brk smoke OK", 10
brk_msg_end:
mmap_msg:
    db "[user] mmap smoke OK", 10
mmap_msg_end:
waitpid_msg:
    db "[user] waitpid smoke OK", 10
waitpid_msg_end:
fork_child_msg:
    db "[user] fork child OK", 10
fork_child_msg_end:
fork_msg:
    db "[user] fork/waitpid COW smoke OK", 10
fork_msg_end:
fail_msg:
    db "[user] smoke failed", 10
fail_msg_end:
fail_mmap_sys_msg:
    db "[user] mmap syscall failed", 10
fail_mmap_sys_msg_end:
fail_mmap_write_msg:
    db "[user] mmap write failed", 10
fail_mmap_write_msg_end:
fail_munmap_msg:
    db "[user] munmap failed", 10
fail_munmap_msg_end:
fail_waitpid_msg:
    db "[user] waitpid failed", 10
fail_waitpid_msg_end:
fail_badptr_msg:
    db "[user] bad user pointer failed", 10
fail_badptr_msg_end:
fail_fork_msg:
    db "[user] fork failed", 10
fail_fork_msg_end:
fail_waitpid_child_msg:
    db "[user] child waitpid failed", 10
fail_waitpid_child_msg_end:
fail_cow_msg:
    db "[user] COW isolation failed", 10
fail_cow_msg_end:
fail_argv_msg:
    db "[user] exec argv/envp failed", 10
fail_argv_msg_end:
arg_expected:
    db "argv-ok", 0
env_expected:
    db "ENV=ok", 0
