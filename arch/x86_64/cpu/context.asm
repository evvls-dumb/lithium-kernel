; =============================================================
; arch/x86_64/cpu/context.asm
;
; context_switch(uint64_t *old_rsp, uint64_t new_rsp)
;   rdi = pointer to old task's rsp storage
;   rsi = new task's rsp value
;
; Saves callee-saved registers on the current stack, stores the
; resulting RSP into *old_rsp, then restores from new_rsp.
; =============================================================

bits 64

global context_switch

; context_switch(uint64_t *old_rsp, uint64_t new_rsp)
; Saves callee-saved registers then restores the new task's.
; Push/pop ORDER: r15,r14,r13,r12,rbp,rbx  (last push = lowest addr).
context_switch:
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx

    mov [rdi], rsp      ; save old RSP into *old_rsp
    mov  rsp, rsi       ; switch to new RSP

    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    ret     ; pops RIP: jumps to task_trampoline or resumes schedule()

; ── task_trampoline ────────────────────────────────────────────
; Called via the ret above for newly-created tasks.
; r12 = fn, r13 = arg (set up in task_create's stack frame).
global task_trampoline
task_trampoline:
    ; When a new task starts via context_switch (which is called from the
    ; timer ISR), RFLAGS.IF may be 0.  Always re-enable interrupts so the
    ; task is preemptible and kbd_getchar's hlt actually wakes up.
    sti
    mov rdi, r13        ; first C argument = arg
    call r12            ; call fn(arg)
    ; If fn ever returns, exit gracefully.
    xor edi, edi
    extern task_exit
    call task_exit
    hlt

; ── user_mode_enter ────────────────────────────────────────────
; Called via context_switch for a newly-created user task.
; r12 = user RIP, r13 = user RSP.
global user_mode_enter
user_mode_enter:
    mov ax, 0x23        ; user data selector (GDT_USER_DATA | RPL3)
    mov ds, ax
    mov es, ax

    push qword 0x23     ; SS
    push r13            ; RSP
    push qword 0x202    ; RFLAGS: IF=1, reserved bit set
    push qword 0x1B     ; CS (GDT_USER_CODE | RPL3)
    push r12            ; RIP
    iretq

; Called via context_switch for a newly-created fork child.
; r12 = copied syscall_frame_t on the child's kernel stack.
extern syscall_return_from_frame
global fork_return_to_user
fork_return_to_user:
    mov rsp, r12
    jmp syscall_return_from_frame
