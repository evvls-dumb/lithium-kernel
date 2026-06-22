; =============================================================
; arch/x86_64/boot/entry.asm
;
; Multiboot2 header + 32-bit → 64-bit long-mode bootstrap.
;
; GRUB loads our kernel ELF and jumps to _start in 32-bit
; protected mode (flat 4 GiB segments, no paging).  We must:
;   1. Verify long-mode support via CPUID.
;   2. Build minimal 4-level page tables (identity map 0–1 GiB).
;   3. Enable PAE, load CR3, set EFER.LME, enable paging.
;   4. Far-jump into 64-bit code and call kmain().
; =============================================================

bits 32

; ── Multiboot2 header (GRUB ISO boot uses this protocol) ─────
; NOTE: Multiboot1 is intentionally omitted — it only supports 32-bit ELF
;       and would cause QEMU to reject our 64-bit image. QEMU direct boot
;       is handled via the PVH ELF note below.
MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0                              ; i386 (protected mode)
MB2_LEN      equ (header_end - header_start)
MB2_CHECK    equ (-(MB2_MAGIC + MB2_ARCH + MB2_LEN)) & 0xFFFFFFFF

section .multiboot2
align 8
header_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_LEN
    dd MB2_CHECK
    ; End tag (type=0, flags=0, size=8)
    align 8
    dw 0
    dw 0
    dd 8
header_end:

; ── Xen PVH ELF note (QEMU 9+ uses this for ELF -kernel boot) ──
; QEMU 9/10 changed its ELF loader to require a PVH note.
; On PVH boot: EAX = 0x336ec578 (XEN_HVM_START_MAGIC_VALUE),
;              EBX = physical address of start_info struct.
; Our _start handles all three boot paths identically.
XEN_ELFNOTE_PHYS32_ENTRY equ 18

section .note.Xen
align 4
    dd 4                        ; namesz = len("Xen\0")
    dd 4                        ; descsz = sizeof(uint32_t) — the entry PA
    dd XEN_ELFNOTE_PHYS32_ENTRY ; note type
    db "Xen", 0                 ; note name (4 bytes, 4-aligned)
    dd _start                   ; 32-bit physical entry point

; ── Early data – save boot args across the mode switch ────────
section .data
mb2_magic_saved: dd 0
mb2_info_saved:  dd 0

; ── BSS: boot page tables + stack ────────────────────────────
section .bss
align 4096
boot_pml4: resb 4096    ; Page-Map Level-4
boot_pdpt: resb 4096    ; Page Directory Pointer Table
boot_pd:   resb 4096    ; Page Directory (2 MiB entries)

align 16
stack_bottom: resb 16384    ; 16 KiB initial stack
stack_top:

; Export both ends of the boot stack for TSS and VMM guard-page setup.
global stack_bottom
global stack_top

; ── 32-bit entry point ───────────────────────────────────────
section .text
global _start
extern kmain

_start:
    ; eax = Multiboot2 magic, ebx = info struct physical address
    mov [mb2_magic_saved], eax
    mov [mb2_info_saved],  ebx

    ; Set up a temporary stack (physical address, same as virtual pre-paging).
    mov esp, stack_top

    ; ── Check long-mode support ──────────────────────────────
    ; First confirm extended CPUID leaves exist.
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb  .no_long_mode

    ; Check LM bit (bit 29 of EDX from leaf 80000001h).
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)
    jz  .no_long_mode

    ; ── Zero all three page-table pages ─────────────────────
    ; (BSS is not guaranteed zeroed by GRUB before our code runs.)
    mov edi, boot_pml4
    xor eax, eax
    mov ecx, (4096 * 3) / 4
    rep stosd

    ; ── Build identity-map for 0 – 1 GiB (2 MiB pages) ─────
    ;
    ; PML4[0]  -> PDPT   (covers virtual 0 – 512 GiB)
    ; PDPT[0]  -> PD     (covers virtual 0 –   1 GiB)
    ; PD[0..511] = 2 MiB huge pages (P | RW | PS)

    ; PML4[0] = &boot_pdpt | Present | Writable
    mov eax, boot_pdpt
    or  eax, 0x03
    mov [boot_pml4], eax
    ; Upper 32 bits stay zero (already zeroed above).

    ; PDPT[0] = &boot_pd | Present | Writable
    mov eax, boot_pd
    or  eax, 0x03
    mov [boot_pdpt], eax

    ; PD[i] = (i * 2 MiB) | Present | Writable | PageSize
    mov edi, boot_pd
    mov eax, 0x83           ; flags: P | RW | PS
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    ; Upper dword of each 8-byte PDE is already zero.
    add eax, 0x200000       ; next 2 MiB frame
    add edi, 8
    loop .fill_pd

    ; ── Load CR3 ─────────────────────────────────────────────
    mov eax, boot_pml4
    mov cr3, eax

    ; ── Enable PAE (required for 4-level paging) ─────────────
    mov eax, cr4
    or  eax, (1 << 5)       ; CR4.PAE
    mov cr4, eax

    ; ── Set EFER.LME (Long Mode Enable) ──────────────────────
    mov ecx, 0xC0000080     ; IA32_EFER MSR
    rdmsr
    or  eax, (1 << 8)       ; EFER.LME
    wrmsr

    ; ── Enable paging (activates long-mode) ──────────────────
    mov eax, cr0
    or  eax, (1 << 31) | 1  ; CR0.PG | CR0.PE
    mov cr0, eax

    ; ── Load a minimal 64-bit GDT and far-jump ───────────────
    lgdt [gdt64_ptr]
    jmp  0x08:long_mode_entry   ; CS = 0x08, jump to 64-bit stub

; ── Error path: no long-mode support ─────────────────────────
.no_long_mode:
    ; Print "NO LM" in red at top-left of the VGA text buffer.
    mov dword [0xB8000], 0x4C4F4C4E  ; 'NL' red-on-black
    mov dword [0xB8004], 0x4C4D4C20  ; ' M'
    cli
    hlt

; ── Minimal bootstrap GDT (replaced by gdt_init() later) ─────
align 8
gdt64:
    dq 0                        ; 0x00  null descriptor
    dq 0x00AF9A000000FFFF       ; 0x08  64-bit kernel code (L=1, DPL=0)
    dq 0x00AF92000000FFFF       ; 0x10  64-bit kernel data (DPL=0)
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1   ; limit
    dd gdt64                    ; base (32-bit physical, fits here)

; ── 64-bit long-mode entry ────────────────────────────────────
bits 64
long_mode_entry:
    ; Reload data segment registers (CS already set by the far jump).
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; In 64-bit mode, writing to a 32-bit sub-register zero-extends
    ; the full 64-bit register.  Use this to safely load the saved
    ; Multiboot2 values into rdi / rsi for kmain().
    xor rdi, rdi
    xor rsi, rsi
    mov edi, [mb2_magic_saved]   ; arg0: magic
    mov esi, [mb2_info_saved]    ; arg1: info pointer (physical)

    ; Switch to the proper 64-bit stack.
    mov rsp, stack_top

    call kmain

.halt:
    cli
    hlt
    jmp .halt
