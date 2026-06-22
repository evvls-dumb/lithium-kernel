; =============================================================
; arch/x86_64/cpu/idt.asm
;
; 256 per-vector ISR stubs + the common save/restore wrapper.
;
; Each stub normalises the stack to:
;   [rsp+ 0]  vector number
;   [rsp+ 8]  error code (CPU-pushed or dummy 0)
;   [rsp+16]  rip / cs / rflags / rsp / ss  (CPU)
;
; isr_common_stub then saves all GPRs and calls isr_handler(frame).
;
; A table (isr_table) of 256 function pointers lets idt_init() in
; C fill the IDT without declaring 256 extern symbols.
; =============================================================

bits 64

extern isr_handler
global isr_common_stub
global isr_table

; ── Macros ───────────────────────────────────────────────────
; Exception WITHOUT a CPU-pushed error code — push dummy 0 first.
%macro ISR_NOERR 1
isr_%1:
    push 0
    push %1
    jmp isr_common_stub
%endmacro

; Exception WITH a CPU-pushed error code (already on stack).
%macro ISR_ERR 1
isr_%1:
    push %1
    jmp isr_common_stub
%endmacro

; ── CPU Exceptions (vectors 0–31) ────────────────────────────
ISR_NOERR  0    ; #DE  Divide-by-Zero
ISR_NOERR  1    ; #DB  Debug
ISR_NOERR  2    ; #NMI Non-Maskable Interrupt
ISR_NOERR  3    ; #BP  Breakpoint
ISR_NOERR  4    ; #OF  Overflow
ISR_NOERR  5    ; #BR  Bound Range Exceeded
ISR_NOERR  6    ; #UD  Invalid Opcode
ISR_NOERR  7    ; #NM  Device Not Available
ISR_ERR    8    ; #DF  Double Fault            (error code = 0, always)
ISR_NOERR  9    ; —    Coprocessor Segment Overrun (legacy)
ISR_ERR   10    ; #TS  Invalid TSS
ISR_ERR   11    ; #NP  Segment Not Present
ISR_ERR   12    ; #SS  Stack Segment Fault
ISR_ERR   13    ; #GP  General Protection Fault
ISR_ERR   14    ; #PF  Page Fault
ISR_NOERR 15    ; —    Reserved
ISR_NOERR 16    ; #MF  x87 FP Exception
ISR_ERR   17    ; #AC  Alignment Check
ISR_NOERR 18    ; #MC  Machine Check
ISR_NOERR 19    ; #XM  SIMD FP Exception
ISR_NOERR 20    ; #VE  Virtualisation Exception
ISR_ERR   21    ; #CP  Control Protection
ISR_NOERR 22    ; —    Reserved
ISR_NOERR 23    ; —    Reserved
ISR_NOERR 24    ; —    Reserved
ISR_NOERR 25    ; —    Reserved
ISR_NOERR 26    ; —    Reserved
ISR_NOERR 27    ; —    Reserved
ISR_NOERR 28    ; #HV  Hypervisor Injection
ISR_ERR   29    ; #VC  VMM Communication
ISR_ERR   30    ; #SX  Security Exception
ISR_NOERR 31    ; —    Reserved

; ── Hardware IRQs (vectors 32–47, PIC-remapped) ──────────────
%assign i 32
%rep 16
ISR_NOERR i
%assign i i+1
%endrep

; ── Remaining software/spurious vectors (48–255) ─────────────
%assign i 48
%rep 208
ISR_NOERR i
%assign i i+1
%endrep

; ── Common save/restore wrapper ───────────────────────────────
isr_common_stub:
    ; Save all caller- and callee-saved GPRs in the order that
    ; matches interrupt_frame_t (lowest field = lowest address).
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

    ; Pass a pointer to the frame as the first argument (System V ABI).
    mov rdi, rsp

    ; Align stack to 16 bytes as required by the ABI before a call.
    ; We've pushed 15 GPRs + vector + error_code = 17 qwords (136 B)
    ; on top of the CPU's 5 qwords (40 B) = 176 B total; 176 % 16 = 0. ✓
    call isr_handler

    ; Restore GPRs in reverse push order.
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

    ; Discard the vector number and error code pushed by our stubs.
    add rsp, 16

    iretq

; ── Jump table: isr_table[256] ────────────────────────────────
; Lets idt_init() iterate instead of declaring 256 extern symbols.
section .rodata
isr_table:
    %assign i 0
    %rep 256
        dq isr_%+i
    %assign i i+1
    %endrep
