bits 64
default rel

global _start

section .text
_start:
    cld
    sub rsp, 16

    mov eax, 1
    mov edi, 1
    lea rsi, [start_msg]
    mov edx, start_msg_end - start_msg
    syscall
    cmp rax, start_msg_end - start_msg
    jne fail

    mov eax, 57
    syscall
    test rax, rax
    js fail_fork
    jz .child

    mov r12, rax
    mov dword [rsp], 0

.wait_child:
    mov eax, 61
    mov rdi, r12
    mov rsi, rsp
    xor edx, edx
    syscall
    cmp rax, r12
    jne fail_wait
    cmp dword [rsp], 0
    jne fail_wait

    mov eax, 1
    mov edi, 1
    lea rsi, [reaped_msg]
    mov edx, reaped_msg_end - reaped_msg
    syscall
    cmp rax, reaped_msg_end - reaped_msg
    jne fail

    call orphan_reaper_smoke
    call shell_smoke

    mov eax, 1
    mov edi, 1
    lea rsi, [resident_msg]
    mov edx, resident_msg_end - resident_msg
    syscall
    cmp rax, resident_msg_end - resident_msg
    jne fail
    jmp launch_shell_loop

.reaper_loop:
    mov dword [rsp], 0
    mov eax, 61
    mov rdi, -1
    mov rsi, rsp
    mov edx, 1
    syscall
    test rax, rax
    jg .reaper_loop
    cmp rax, 0
    je .yield
    cmp rax, -10
    jne fail_wait

.yield:
    mov eax, 24
    syscall
    jmp .reaper_loop

.child:
    mov eax, 59
    lea rdi, [smoke_path]
    lea rsi, [argv_vec]
    lea rdx, [envp_vec]
    syscall

    lea rsi, [exec_fail_msg]
    mov edx, exec_fail_msg_end - exec_fail_msg
    jmp fail_print

fail_fork:
    lea rsi, [fork_fail_msg]
    mov edx, fork_fail_msg_end - fork_fail_msg
    jmp fail_print

fail_wait:
    lea rsi, [wait_fail_msg]
    mov edx, wait_fail_msg_end - wait_fail_msg
    jmp fail_print

orphan_reaper_smoke:
    sub rsp, 16
    mov dword [rsp], 0
    mov eax, 57
    syscall
    test rax, rax
    js fail_fork
    jz .orphan_parent

    mov r13, rax
    mov eax, 61
    mov rdi, r13
    mov rsi, rsp
    xor edx, edx
    syscall
    cmp rax, r13
    jne fail_wait
    cmp dword [rsp], 0x300
    jne fail_wait

    mov dword [rsp], 0
    mov eax, 61
    mov rdi, -1
    mov rsi, rsp
    xor edx, edx
    syscall
    test rax, rax
    jle fail_wait
    cmp rax, r13
    je fail_wait
    cmp dword [rsp], 0x900
    jne fail_wait

    mov eax, 1
    mov edi, 1
    lea rsi, [orphan_msg]
    mov edx, orphan_msg_end - orphan_msg
    syscall
    cmp rax, orphan_msg_end - orphan_msg
    jne fail
    add rsp, 16
    ret

.orphan_parent:
    mov eax, 57
    syscall
    test rax, rax
    js fail_fork
    jz .orphan_child

    mov eax, 60
    mov edi, 3
    syscall

.orphan_child:
    mov eax, 60
    mov edi, 9
    syscall

shell_smoke:
    sub rsp, 16
    mov dword [rsp], 0
    mov eax, 57
    syscall
    test rax, rax
    js fail_fork
    jz .shell_child

    mov r13, rax
    mov eax, 61
    mov rdi, r13
    mov rsi, rsp
    xor edx, edx
    syscall
    cmp rax, r13
    jne fail_wait
    cmp dword [rsp], 0
    jne fail_wait

    mov eax, 1
    mov edi, 1
    lea rsi, [shell_msg]
    mov edx, shell_msg_end - shell_msg
    syscall
    cmp rax, shell_msg_end - shell_msg
    jne fail
    add rsp, 16
    ret

.shell_child:
    mov eax, 59
    lea rdi, [shell_path]
    lea rsi, [shell_smoke_argv_vec]
    lea rdx, [shell_envp_vec]
    syscall

    lea rsi, [shell_fail_msg]
    mov edx, shell_fail_msg_end - shell_fail_msg
    jmp fail_print

launch_shell_loop:
.spawn_shell:
    mov dword [rsp], 0
    mov eax, 57
    syscall
    test rax, rax
    js fail_fork
    jz .interactive_child

    mov r14, rax
    mov eax, 1
    mov edi, 1
    lea rsi, [interactive_msg]
    mov edx, interactive_msg_end - interactive_msg
    syscall
    cmp rax, interactive_msg_end - interactive_msg
    jne fail

.wait_any:
    mov dword [rsp], 0
    mov eax, 61
    mov rdi, -1
    mov rsi, rsp
    xor edx, edx
    syscall
    test rax, rax
    jle fail_wait
    cmp rax, r14
    jne .wait_any

    mov eax, 1
    mov edi, 1
    lea rsi, [restart_msg]
    mov edx, restart_msg_end - restart_msg
    syscall
    cmp rax, restart_msg_end - restart_msg
    jne fail
    jmp .spawn_shell

.interactive_child:
    mov eax, 59
    lea rdi, [shell_path]
    lea rsi, [shell_argv_vec]
    lea rdx, [shell_envp_vec]
    syscall

    lea rsi, [shell_fail_msg]
    mov edx, shell_fail_msg_end - shell_fail_msg
    jmp fail_print

fail:
    lea rsi, [fail_msg]
    mov edx, fail_msg_end - fail_msg

fail_print:
    mov eax, 1
    mov edi, 2
    syscall

    mov eax, 60
    mov edi, 1
    syscall

.hang:
    jmp .hang

section .rodata
start_msg:
    db "[user] init starting", 10
start_msg_end:
reaped_msg:
    db "[user] init reaped smoke OK", 10
reaped_msg_end:
orphan_msg:
    db "[user] init orphan reaper OK", 10
orphan_msg_end:
resident_msg:
    db "[user] init resident reaper OK", 10
resident_msg_end:
shell_msg:
    db "[user] init launched sh OK", 10
shell_msg_end:
interactive_msg:
    db "[user] init launched interactive sh OK", 10
interactive_msg_end:
restart_msg:
    db "[user] init restarting sh", 10
restart_msg_end:
fail_msg:
    db "[user] init failed", 10
fail_msg_end:
exec_fail_msg:
    db "[user] init exec smoke failed", 10
exec_fail_msg_end:
fork_fail_msg:
    db "[user] init fork failed", 10
fork_fail_msg_end:
wait_fail_msg:
    db "[user] init wait failed", 10
wait_fail_msg_end:
shell_fail_msg:
    db "[user] init exec sh failed", 10
shell_fail_msg_end:
smoke_path:
    db "/bin/smoke", 0
shell_path:
    db "/bin/sh", 0
smoke_arg:
    db "--smoke", 0
arg_magic:
    db "argv-ok", 0
env_magic:
    db "ENV=ok", 0

align 8
argv_vec:
    dq smoke_path
    dq arg_magic
    dq 0
envp_vec:
    dq env_magic
    dq 0
shell_argv_vec:
    dq shell_path
    dq 0
shell_smoke_argv_vec:
    dq shell_path
    dq smoke_arg
    dq 0
shell_envp_vec:
    dq env_magic
    dq 0
