bits 64

extern syscall_dispatch
extern syscall_kernel_stack_top

global syscall_entry
global syscall_return_from_frame

%define GDT_USER_CODE_RPL3 0x1B
%define GDT_USER_DATA_RPL3 0x23

section .bss
align 8
syscall_saved_user_rsp:
    resq 1

section .text
syscall_entry:
    mov [rel syscall_saved_user_rsp], rsp
    mov rsp, [rel syscall_kernel_stack_top]
    and rsp, -16

    push qword [rel syscall_saved_user_rsp]
    push r11
    push rcx
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call syscall_dispatch

syscall_return_from_frame:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    pop rcx
    pop r11
    pop rdx

    push qword GDT_USER_DATA_RPL3
    push rdx
    push r11
    push qword GDT_USER_CODE_RPL3
    push rcx
    iretq
