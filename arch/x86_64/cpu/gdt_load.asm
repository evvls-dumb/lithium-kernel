; =============================================================
; arch/x86_64/cpu/gdt.asm
;
; gdt_load  — takes a pointer to a gdtr_t in rdi, calls lgdt,
;             then does a far-return to reload CS with the new
;             kernel code selector (0x08) and sets the data
;             segment registers to kernel data (0x10).
;
; tss_load  — takes the TSS selector in di, calls ltr.
; =============================================================

bits 64

global gdt_load
global tss_load

; void gdt_load(const gdtr_t *gdtr)   [rdi = pointer to GDTR]
gdt_load:
    lgdt [rdi]

    ; Reload CS via the "push+retfq" far-return trick:
    ; push the new CS selector, then the return address, then retfq.
    push 0x08                       ; kernel code selector
    lea  rax, [rel .reload_cs]
    push rax
    retfq                           ; pops RIP then CS

.reload_cs:
    mov ax, 0x10                    ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

; void tss_load(uint16_t selector)    [di = TSS selector]
tss_load:
    ltr di
    ret
