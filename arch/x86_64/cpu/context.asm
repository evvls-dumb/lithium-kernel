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
