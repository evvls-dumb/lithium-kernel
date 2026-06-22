# Lithium Kernel — Development Log

> **Format:** Each entry records the date, phase, what was added/changed, problems encountered, and how they were resolved.

---

## 2026-06-17 — Phase 0 & Phase 1 Implementation

### What Was Added

#### Phase 0 — Toolchain & Bootstrap

| File | Purpose |
|------|---------|
| `include/types.h` | Pull in `stdint.h`, `stddef.h`, `stdbool.h` from the freestanding GCC headers |
| `include/io.h` | Inline `outb/inb/outw/inw/outl/inl` and `io_wait()` for port-mapped I/O |
| `include/kernel.h` | Common macros: `KASSERT`, `ARRAY_SIZE`, `ALIGN_UP/DOWN`, `BIT`, `MIN/MAX`, `UNUSED` |
| `lib/string.c/h` | Freestanding `memset`, `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy` |
| `drivers/char/vga.c/h` | 80×25 VGA text-mode driver with colour, scrolling, tab/CR/LF handling |
| `drivers/char/serial.c/h` | COM1 UART 16550 driver at 38 400 baud; used as secondary debug output |
| `lib/kprintf.c/h` | Kernel `printf` writing to both VGA and serial; supports `%d %u %x %X %p %s %c %% %l*` |
| `arch/x86_64/boot/entry.asm` | Multiboot2 header, 32→64-bit long-mode bootstrap, initial identity page tables |
| `linker.ld` | Links kernel at physical 0x100000 (1 MiB); defines `_bss_start`, `_bss_end`, `_kernel_end` |
| `Makefile` | `make` / `make iso` / `make run` / `make run-serial` / `make debug` / `make clean` |

#### Phase 1 — CPU Initialisation

| File | Purpose |
|------|---------|
| `arch/x86_64/cpu/msr.h` | Inline `rdmsr` / `wrmsr`; defines `MSR_EFER`, `MSR_STAR`, `MSR_LSTAR`, `MSR_FMASK` and EFER bits |
| `arch/x86_64/cpu/tss.c/h` | 64-bit TSS; `tss_init(rsp0)`, `tss_get()`, `tss_set_rsp0()` |
| `arch/x86_64/cpu/gdt.c/h` | 7-entry GDT (null, kcode, kdata, ucode, udata, TSS×2); `gdt_init()` |
| `arch/x86_64/cpu/gdt.asm` | `gdt_load` (lgdt + far-return CS reload) and `tss_load` (ltr) |
| `arch/x86_64/cpu/idt.c/h` | IDT with 256 gates, PIC 8259 remap (IRQ0-7→0x20, IRQ8-15→0x28), all IRQs masked |
| `arch/x86_64/cpu/idt.asm` | 256 ISR stubs (NOERR/ERR macros), `isr_common_stub` (saves/restores all GPRs), `isr_table[256]` |
| `arch/x86_64/cpu/cpuid.c/h` | CPUID feature detection: APIC, x2APIC, SSE/SSE2/AVX/AVX2, NX, SYSCALL, 1 GiB pages |
| `kernel/panic.c/h` | `panic(msg)` and `panic_exception(frame)` — coloured full register dump then `cli;hlt` |
| `kernel/kassert.c` | Implements `_kassert_fail` backing the `KASSERT()` macro |
| `kernel/main.c` | `kmain()` — calls all init functions in order; leaves CPU in idle `hlt` loop |

---

### Design Decisions

#### 1. Kernel linked at physical 1 MiB, not higher-half

**Decision:** Link the Phase 0/1 kernel at virtual = physical 0x100000 (1 MiB).  
**Rationale:** Doing the 32→64 bit bootstrap AND higher-half relocation at the same time in assembly is error-prone and hard to debug. We establish a working baseline with identity mapping first. Phase 3 will move to `0xFFFFFFFF80000000`.  
**Impact:** The linker script uses `. = 0x100000`. `virtual == physical` for all kernel addresses right now.

#### 2. 2 MiB huge pages for the bootstrap page tables

**Decision:** Map the first 1 GiB as 512 × 2 MiB pages using the PS (PageSize) bit in the PD.  
**Rationale:** Avoids allocating 512 × 4 KiB PT pages just to bootstrap. 1 GiB easily covers our small kernel + stack + VGA buffer. The real VMM (Phase 3) will use 4 KiB pages.

#### 3. Multiboot2 arguments saved to `.data` before mode switch

**Problem:** Going from 32-bit to 64-bit protected mode is a hard cut. Registers are preserved through the far-jump but in pure 32-bit mode the processor does not zero-extend into the 64-bit halves of registers. The upper 32 bits of rdi/rsi would be implementation-defined garbage after the mode switch.  
**Solution:** `_start` stores `eax` (MB2 magic) and `ebx` (MB2 info ptr) into two `dd` variables in `.data` before touching any registers. In `long_mode_entry` we read them back with zero-extending moves (`mov edi, [...]` in 64-bit mode zero-extends to `rdi`). This is the most reliable approach.

#### 4. ISR jump table (`isr_table`) instead of 256 extern declarations

**Problem:** Declaring `extern void isr_N(void)` for all 256 vectors in C is 256 lines of noise and error-prone.  
**Solution:** `idt.asm` exports a `.rodata` section array `isr_table: dq isr_0, dq isr_1, ...` built with a `%rep 256` NASM macro loop. `idt_init()` just iterates `isr_table[0..255]`. Clean, zero C boilerplate.

#### 5. Error-code normalisation in ISR stubs

**Problem:** 10 CPU exceptions push a 64-bit error code; the other 246 vectors do not. This creates an inconsistent stack layout at the common stub.  
**Solution:** Two NASM macros — `ISR_NOERR` pushes a dummy `0` then the vector number; `ISR_ERR` pushes only the vector number (CPU already pushed the error code). Both stubs arrive at `isr_common_stub` with an identical stack layout: `[rsp]=vector, [rsp+8]=error_code`. The `interrupt_frame_t` struct in `idt.h` documents this layout exactly.

#### 6. PIC 8259 remapped immediately in `idt_init`

**Problem:** On x86, the default IRQ vectors (0x08-0x0F for master PIC) collide with CPU exception vectors. Before remapping, any hardware IRQ fires an exception handler instead.  
**Solution:** Remap master PIC → 0x20-0x27, slave PIC → 0x28-0x2F, then mask all IRQs. The STI at the end of `kmain` enables interrupts but nothing fires since all lines are masked. Phase 5 will unmask specific IRQs as their drivers are installed.

#### 7. MSR setup in `kmain`, not a separate file

**Decision:** EFER.NXE + EFER.SCE + STAR/LSTAR/FMASK are set directly in `kmain` after CPUID reports whether they're available.  
**Rationale:** There's only a handful of MSR writes in Phase 1. Splitting them into a separate `msr_init()` function with a stub LSTAR would add a file without adding clarity. When Phase 8 adds the real SYSCALL handler, `MSR_LSTAR` will be set there where the handler is defined.

---

### Problems Faced

#### Problem 1 — NASM `%rep` stringification for `isr_table`
**Symptom:** Initially tried `dq isr_%i` inside a `%rep` block. NASM does not do automatic integer-to-string conversion for label concatenation.  
**Fix:** Use `%+` concatenation operator: `dq isr_%+i`. This correctly generates labels `isr_0` through `isr_255`.

#### Problem 2 — Stack alignment at ISR common stub
**Symptom:** GCC-compiled C code called from the ISR handler could fault due to misaligned SSE loads (if SSE is ever used in kernel code, even implicitly by the compiler).  
**Analysis:** On x86-64, the ABI requires 16-byte stack alignment *before* a `call` instruction (which pushes an 8-byte return address, leaving it 8-byte aligned at the callee entry). On interrupt entry the CPU always pushes 5 qwords (40 bytes), our stub pushes 2 more (vector + err = 16 bytes), and the common stub pushes 15 more (GPRs = 120 bytes). Total = 176 bytes. 176 % 16 = 0, so the stack is 16-byte aligned when we `call isr_handler`. ✓  
**Note:** We disable SSE/MMX in the Makefile (`-mno-sse -mno-mmx`) so the compiler never emits aligned SSE loads anyway. This is mandatory for kernel code since SSE state is not saved on interrupt entry.

#### Problem 3 — `gdt_load` CS reload in 64-bit mode
**Symptom:** You cannot do `mov cs, ax` in x86 — CS is read-only; only far jumps/calls/returns load it.  
**Fix:** Use the canonical "retfq trick": push the new CS selector, push the return address (via `lea rax, [rel .reload_cs]`), then `retfq`. The processor pops RIP and CS atomically. This is the standard approach used by every x86-64 OS.

#### Problem 4 — Bootstrap page tables must be zeroed explicitly
**Symptom:** GRUB's Multiboot2 implementation does NOT guarantee BSS is zeroed before handing control to `_start`. If page tables contain garbage, the PDPTE/PDE entries would have random flags and the CPU would triple-fault entering long mode.  
**Fix:** In `_start`, explicitly zero all three page-table pages using `rep stosd` (clearing `4096 * 3 / 4` dwords) before writing any PML4/PDPT/PD entries.

#### Problem 5 — `_kassert_fail` forward declaration conflict
**Symptom:** `kernel.h` declared `void _kassert_fail(...)` without `__attribute__((noreturn))`. GCC warned about control flow falling off the end of `_kassert_fail` call sites.  
**Fix:** Added `__attribute__((noreturn))` to both the declaration in `kernel.h` and the definition in `kassert.c`.

---

### Exit Criteria Status

| Criterion | Status |
|-----------|--------|
| `kprintf("Lithium booting...")` visible in QEMU | Implemented ✓ |
| Division-by-zero triggers formatted panic + register dump | Implemented ✓ (uncomment test in `kmain`) |
| GDT with kernel/user code+data + TSS loaded | Implemented ✓ |
| IDT with 256 gates, PIC remapped | Implemented ✓ |
| CPUID feature detection printed | Implemented ✓ |
| NX + SYSCALL enabled via EFER | Implemented ✓ |

---

---

## 2026-06-17 — Build & Boot Testing (Windows + WSL)

### Environment

- **Host OS:** Windows 11 (Git Bash / MSYS2)
- **Compiler:** Clang 22 / LLD 22 (LLVM for Windows) — cross-compiles to `x86_64-elf`
- **Assembler:** NASM 2.16 (Windows native)
- **Build tool:** `mingw32-make`
- **Emulator:** QEMU 10.2.1 inside WSL2 Ubuntu

### Build Problems & Fixes

#### Problem 1 — `make` not found on Windows
`make` is not in the default Git Bash PATH. MinGW ships `mingw32-make` instead.  
**Fix:** Use `mingw32-make` for all build invocations.

#### Problem 2 — GNU make's built-in `AS` overrides `AS ?= nasm`
GNU make defines `AS := as` internally. The `?=` conditional assignment doesn't override it, so the wrong assembler (GNU `as`) was called for `.asm` files, producing unintelligible errors.  
**Fix:** Changed to `AS := nasm` (unconditional `:=`) so our assignment always wins.

#### Problem 3 — `gdt.asm` / `idt.asm` name collision with `gdt.c` / `idt.c`
Both pairs wanted to produce the same `gdt.o` / `idt.o` output files. Make used the `.c` rule first, leaving stale `.o` files from the wrong compilation unit.  
**Fix:** Renamed `gdt.asm` → `gdt_load.asm` and `idt.asm` → `isr_stubs.asm`.

#### Problem 4 — `stack_top` symbol not exported from entry.asm
`kmain()` references `extern char stack_top[]` to seed `TSS.RSP0`, but the symbol was never declared `global` in the NASM source.  
**Fix:** Added `global stack_top` to `arch/x86_64/boot/entry.asm`.

#### Problem 5 — `make clean` uses `find -delete` which calls Windows `find.exe`
Windows `find.exe` has a completely different syntax from Unix `find`. The `clean` target silently did nothing, leaving stale `.o` files between builds.  
**Fix:** Changed the clean target to use `$(shell find . -name '*.o' -exec rm -f {} \;)` which runs through Git Bash's Unix `find`.

#### Problem 6 — QEMU 10 rejects ELF kernel without PVH note
QEMU 9/10 changed its ELF `-kernel` loader to require either a Linux setup header or a Xen PVH ELF note. Our Multiboot2 kernel had neither, producing:  
`Error loading uncompressed kernel without PVH ELF Note`  
**Fix:** Added a `.note.Xen` section with a `XEN_ELFNOTE_PHYS32_ENTRY` (type 18) note pointing to `_start`.

#### Problem 7 — PT_NOTE program header missing from ELF
QEMU scans `PT_NOTE` *program header segments*, not ELF sections by name. LLD with a custom linker script only created `PT_LOAD` + `PT_GNU_STACK` headers. The `.note.Xen` section bytes were present but invisible to QEMU's note scanner.  
**Fix:** Added `PHDRS { pvh_note PT_NOTE ... }` to `linker.ld` and assigned `.note.Xen` to both `:pvh_note` and `:rx` so it appears in both a `PT_NOTE` segment (for QEMU to find) and a `PT_LOAD` segment (so the bytes are actually loaded into memory).

#### Problem 8 — Multiboot1 header causes "Cannot load x86-64 image"
Adding a Multiboot1 header (attempted as a simpler QEMU boot path) made QEMU scan the first 8 KiB, find the `0x1BADB002` magic, switch to its Multiboot1 loader, and reject the kernel because Multiboot1 only accepts 32-bit ELF.  
**Fix:** Removed the Multiboot1 header entirely. QEMU direct boot uses PVH; ISO boot uses Multiboot2 via GRUB.

#### Problem 9 — Unknown boot magic (EAX = 0 on PVH entry)
The `XEN_HVM_START_MAGIC_VALUE` (`0x336ec578`) on PVH boot lives in the `hvm_start_info` struct pointed to by EBX, **not** in EAX directly. QEMU does not set EAX to any known magic on PVH entry, so our strict magic check panicked.  
**Fix:** Changed the magic check to a warning (print the received EAX value) and continue. A proper PVH early-init reading EBX→start_info→magic will be added in Phase 6 when the memory map is parsed.

### Final Boot Output (QEMU 10.2.1, PVH, serial)

```
  _     _ _   _     _
 | |   (_) |_| |__ (_)_   _ _ __ ___
 | |   | | __| '_ \| | | | | '_ ` _ \
 | |___| | |_| | | | | |_| | | | | | |
 |_____|_|\__|_| |_|_|\__,_|_| |_| |_|

  Lithium Kernel — Phase 0/1 Bootstrap
  Kernel image end : 0x0000000000110000
  MB2 info addr    : 0x000021c0

[!!] Unknown boot magic 0x00000000 — assuming PVH/direct ELF boot
[  ] Initialising TSS...
[OK] TSS initialised (RSP0 = 0x000000000010e000)
[  ] Initialising GDT...
[OK] GDT loaded (7 entries; TSS descriptor installed)
[  ] Initialising IDT + PIC...
[OK] IDT loaded (256 gates; PIC remapped to 0x20-0x2F; all IRQs masked)
[  ] Detecting CPU features...
CPU vendor  : AuthenticAMD
Max leaf    : 0x0000000d (ext: 0x8000000a)
Features    : APIC SSE SSE2 NX SYSCALL
[OK] NX bit enabled (EFER.NXE)
[OK] SYSCALL/SYSRET enabled (EFER.SCE)
[OK] EFER = 0x0000000000000d01
[OK] Interrupts enabled (STI)

==================================================
  Phase 0 (Bootstrap)     COMPLETE
  Phase 1 (CPU Init)      COMPLETE
==================================================
  Kernel idle. Next: Phase 2 (Physical Memory Manager)
```

**All Phase 0 and Phase 1 exit criteria verified.**

### Next Steps (Phase 4)

- Slab allocator for fixed-size objects (8 B – 2 KiB)
- Buddy allocator for large allocations backed by VMM pages
- `kmalloc(size)` / `kfree(ptr)` / `krealloc(ptr, size)`
- Red-zone / canary checking in debug builds
- `kmalloc_stats()` for heap diagnostics

---

## 2026-06-22 — Phase 3: Virtual Memory Manager

### What Was Added

| File | Purpose |
|------|---------|
| `kernel/mm/vmm.h` | VMM public API — map/unmap, walk, address-space ops, kheap alloc |
| `kernel/mm/vmm.c` | 4-level page-table manager; `vmm_init`, all API functions |

**Modified files:**
- `arch/x86_64/boot/entry.asm` — exported `stack_bottom` (needed for guard page)
- `kernel/panic.c` — `#PF` case reads CR2, decodes error code bits
- `kernel/main.c` — Phase 3 init + full VMM self-test + null-pointer exit criterion

### Design Decisions

#### 1. Two-tier identity mapping (4 KiB + 2 MiB huge pages)

**Decision:** The first 2 MiB of identity mapping uses 4 KiB page tables; 2 MiB–1 GiB uses 2 MiB huge pages.  
**Rationale:** The kernel image lives in the first 2 MiB (0x100000–0x134000). Using 4 KiB granularity here lets us punch exact holes for the null page (0x0) and the guard page (below `stack_bottom`) without touching the rest of the kernel. Huge pages for the rest keep the page-table footprint small.

#### 2. Higher-half kernel alias via single 2 MiB huge page

**Decision:** `KERNEL_VMA (0xFFFFFFFF80000000)` is mapped with one 2 MiB huge page pointing to physical 0x0. The kernel at physical 0x100000 is therefore also accessible at virtual `0xFFFFFFFF80100000`.  
**Rationale:** One huge page covers the entire kernel image and avoids allocating 512 PT frames. This is an alias; the kernel still runs from physical/identity addresses (no linker relocation yet — Phase 6 is the right time to do that alongside process management).

#### 3. Kernel virtual heap starts at next 2 MiB boundary above kernel end

**Decision:** `vmm_alloc_kernel` starts at `ALIGN_UP(KERNEL_VMA + _kernel_end, 2 MiB)`.  
**Rationale:** The higher-half huge page covers `KERNEL_VMA + 0x0` to `KERNEL_VMA + 0x1FFFFF`. `vmm_map_page` correctly refuses to subdivide a huge page. Starting the heap at the next 2 MiB boundary (`0xFFFFFFFF80200000`) ensures every `vmm_map_page` call lands outside the huge page, where fine-grained PT entries are created on demand.

#### 4. Guard page = page physically below `stack_bottom`

**Decision:** The guard page is skipped in PT[0] when building the 4 KiB PT for 0–2 MiB.  
**Rationale:** `stack_bottom` is at `0x10C000` in BSS. `stack_bottom - PAGE_SIZE = 0x10B000` is the `boot_pd` frame (no longer used as a page table after VMM init). Leaving PT[0x10B] clear makes any stack overflow immediately trigger a `#PF` rather than silently corrupting adjacent data.

#### 5. Physical == virtual for page-table frames throughout Phase 3

**Decision:** All page-table frames are allocated from the PMM (low physical memory) and accessed directly via their physical address (identity mapped).  
**Rationale:** Since the identity map covers 0x1000–1 GiB and PMM allocates frames just above `_kernel_end` (0x134000+), all page-table frames are in the identity-mapped range. `(pte_t *)(uintptr_t)phys_frame` always works. This assumption holds until Phase 6 removes the identity map.

### Problems Faced

#### Problem 1 — `vmm_map_page` returning -1 for higher-half addresses

**Root cause:** `vmm_alloc_kernel` was initialising `kheap_next` to `KERNEL_VMA + _kernel_end = 0xFFFFFFFF80134000`. This address sits inside the 2 MiB huge page at `pd_hh[0]` (which covers `0xFFFFFFFF80000000–0xFFFFFFFF801FFFFF`). `vmm_map_page` correctly refused to subdivide it and returned -1. The KASSERT on `rc == 0` fired.  
**Fix:** Align `kheap_next` up to the next 2 MiB boundary: `ALIGN_UP(KERNEL_VMA + _kernel_end, 0x200000)` = `0xFFFFFFFF80200000`. This lands in `pd_hh[1]`, which has no existing entry, so `pt_walk_or_alloc` allocates a fresh PT frame and the map succeeds.

### Exit Criteria Status

| Criterion | Status |
|-----------|--------|
| 4-level page tables active after `vmm_init` | ✓ CR3 = 0x134000 |
| Null page `0x0–0xFFF` unmapped | ✓ `vmm_get_physical` returns `PMM_ALLOC_FAILED` |
| Guard page `0x10B000` unmapped | ✓ verified via `vmm_get_physical` |
| Higher-half alias `0xFFFFFFFF80102b20 → 0x102b20` | ✓ |
| `vmm_map_page` + `vmm_unmap_page` working | ✓ |
| `vmm_create_address_space` shares kernel half | ✓ |
| `vmm_alloc_kernel` returns sequential page-aligned addresses | ✓ |
| Null pointer dereference → `#PF: not-present \| read \| kernel-mode @ 0x0` | ✓ |

---

## 2026-06-21 — Phase 2: Physical Memory Manager

### What Was Added

| File | Purpose |
|------|---------|
| `include/multiboot2.h` | MB2 info / tag / mmap entry structures |
| `arch/x86_64/boot/pvh.h` | Xen PVH `hvm_start_info` + `hvm_memmap_entry` |
| `kernel/mm/pmm.h` | PMM public API header |
| `kernel/mm/pmm.c` | Bitmap allocator: init, alloc, free, contiguous, stats, selftest |

### Design Decisions

#### 1. Static 128 KiB bitmap in BSS

**Decision:** Fixed `uint8_t pmm_bitmap[PMM_BITMAP_BYTES]` (128 KiB) in `.bss`, covering up to 4 GiB physical address space.  
**Rationale:** Dynamic allocation requires a heap (Phase 4). The bitmap is pre-sized for 4 GiB which covers any QEMU test configuration. For a machine with more RAM, the bitmap size would need to grow — but that is a Phase 3/4 concern when a VMM can map the bitmap into virtual memory.

#### 2. Dual memory-map protocol support

**Decision:** `pmm_init` dispatches on `boot_magic`: MB2 magic → parse Multiboot2 tags; otherwise → attempt PVH `hvm_start_info` parse at `boot_info_phys`.  
**Rationale:** We boot via PVH on QEMU and will boot via MB2 on GRUB ISO. Both paths produce the same internal result (usable region list), so the rest of the PMM is protocol-agnostic.

#### 3. Start-used, mark-free strategy

**Decision:** `memset(bitmap, 0xFF)` first (all frames used), then mark usable regions free, then re-mark reserved regions (0–1 MiB + kernel image) used again.  
**Rationale:** Ensures correctness regardless of memory map ordering. Any memory not explicitly listed as usable stays reserved, which is the safe default. A "start-free, mark-used" strategy would be wrong if the memory map contains gaps.

#### 4. Frame 0 always reserved

**Decision:** The entire 0x0–0x100000 range is re-marked used after parsing.  
**Rationale:** Frame 0 (physical 0x0) must never be allocated — returning physical address 0 would make it indistinguishable from `NULL`. The entire first 1 MiB also contains the BIOS data area, VGA frame buffer, and BIOS ROM.

#### 5. Self-test uses a static array (not stack)

**Decision:** `static uint64_t frames[1000]` in `pmm_selftest`.  
**Rationale:** 8 000 bytes on an already-partially-used 16 KiB boot stack is risky. A static array has zero stack cost and is zeroed by the BSS guarantee.

### Problems Faced

#### Problem 1 — `pmm_total_frames` set to 1 048 576 (4 GiB) despite only 256 MiB

**Root cause:** Entry [6] in the PVH memory map: `0xfd00000000 + 0x300000000` — this is QEMU's PCI MMIO region near the 4 GiB boundary. The entry type is `reserved` but its top address pushes `pmm_total_frames` to 4 GiB (the clamped maximum).  
**Impact:** The bitmap scan covers all 1 M frames but only ~65 K of them are actually free. Allocation is still correct; the scan is O(bitmap_bytes) = O(128 K) which is fast enough for Phase 2.  
**Mitigation for Phase 3+:** Track `pmm_total_frames` as the top of the highest *usable* entry, not the highest *any* entry. This shrinks the effective bitmap range to ~256 MiB and speeds up scans.

#### Problem 2 — Double-free assert in self-test during contiguous alloc verification

**Root cause:** Initial contiguous free loop called `pmm_free_frame(base + i * FRAME_SIZE)` inside the same loop that checked `!frame_is_free`. After freeing frame N, when checking frame N+1, N was already free — the `KASSERT(!frame_is_free(f))` in `pmm_free_frame` would fire on the second call.  
**Fix:** Removed the redundant post-free check. `pmm_free_frame` already contains a double-free guard via `KASSERT(!frame_is_free(f))` inside itself.

### Exit Criteria Status

| Criterion | Status |
|-----------|--------|
| Memory map parsed and printed | ✓ 7 PVH entries, 254 MiB usable |
| Bitmap allocator initialised | ✓ 65 197 free frames |
| `pmm_alloc_frame` / `pmm_free_frame` working | ✓ |
| Reserved regions correctly marked | ✓ 0–1 MiB + kernel 0x100000–0x133000 |
| `pmm_alloc_contiguous(16)` returns 16 contiguous frames | ✓ at 0x133000 |
| 1 000-frame alloc/free self-test passes | ✓ |
| Kernel reaches Phase 2 COMPLETE banner | ✓ |

---

## 2026-06-22 — Phases 4–15: Full Kernel Stack to Interactive Shell

### Goal
Build everything from the kernel heap to an interactive Arch-style command-line shell.

### What Was Added

| File | Phase | Purpose |
|------|-------|---------|
| `kernel/mm/heap.h / heap.c` | 4 | Slab allocator (16–1024 B, 7 classes) + large alloc via VMM |
| `kernel/irq.h` | 5 | IRQ registration/dispatch framework (`irq_register`, `irq_mask/unmask`) |
| `kernel/timer.h / timer.c` | 5 | PIT 8253 @ 1000 Hz, `jiffies`, `timer_ms()` |
| `drivers/char/ps2kbd.h / ps2kbd.c` | 11 | PS/2 keyboard, IRQ1, auto-detect set 1/set 2, shift/caps/ctrl, 256-byte circular buffer |
| `kernel/sched/task.h / task.c` | 6 | `struct task`, `task_create()`, `task_exit()`, 8 KiB kernel stack per task |
| `arch/x86_64/cpu/context.asm` | 6 | `context_switch(old_rsp, new_rsp)` + `task_trampoline` (sti + fn(arg)) |
| `kernel/sched/sched.h / sched.c` | 7 | Circular run queue, `sched_add/remove`, `schedule()`, `sched_start()`, idle task |
| `kernel/fs/vfs.h / vfs.c` | 10 | VFS core (vnode, vfs_ops, FD table), tmpfs (dynamic buffer files), devfs |
| `kernel/shell/shell.h / shell.c` | 15 | Interactive shell: readline with history, 15 built-in commands |
| Updated `arch/x86_64/cpu/idt.c` | 5 | IRQ dispatch, `irq_register/mask/unmask`, PIC EOI |
| Updated `lib/string.c/h` | — | Added `strchr`, `strrchr`, `strcat`, `strncat` |
| Updated `kernel/panic.c` | 3 | `#PF` reads CR2, decodes error code bits |
| Updated `lib/kprintf.c` | — | CLI/STI around print to prevent re-entrancy corruption |

### Shell Commands

```
help      uname [-a]    uptime     mem        cpu
tasks     ls [path]     cat <file> echo [-n]  write <f> <text>
clear     history       panic      reboot     poweroff
```

### Design Decisions

#### Cooperative Scheduling (Phase 13 dependency)
**Decision:** Timer IRQ does NOT call `schedule()`. Shell runs directly from kmain.
**Rationale:** Without spinlocks (Phase 13), the preemptive scheduler preempts `kprintf` mid-call, corrupting `vga_row/vga_col` and `serial_putchar` state. The two trampolines (`[tramp]` debug confirmed this). Making `kprintf` interrupt-safe with `pushfq/cli/popfq` helped but the fundamental issue is that VGA state is globally mutable. Full preemption requires spinlocks around all shared state.
**Fix:** Preemptive `sched_tick()` re-enabled in Phase 13 once spinlocks are implemented.

#### Slab Allocator (no buddy system yet)
**Decision:** Fixed 7 size classes (16–1024 B), large allocs directly via VMM.
**Rationale:** Phase 4 doesn't need a full buddy system. Large allocations (> 1024 B) get their own pages from `vmm_alloc_kernel` + PMM frames. The slab handles the common case (task structs, vnodes, string buffers). Buddy system can be added in Phase 4 completion.

#### tmpfs as rootfs, devfs inline
**Decision:** Single `vfs.c` file contains both tmpfs and devfs implementations.
**Rationale:** Proper FS driver separation adds file count without adding clarity at Phase 10's scope. The VFS ops table (`vfs_ops_t`) allows future split when ext2 is needed. For now `/`, `/etc`, `/dev`, `/proc`, `/tmp` are all tmpfs with devfs nodes inline.

#### task_trampoline in assembly
**Decision:** `task_trampoline` is an ASM stub that does `sti` then `call r12` with `r13` as arg.
**Rationale:** When a new task starts via `context_switch` (called from `sched_start` in kmain), the CPU's IF flag may or may not be set. The trampoline guarantees interrupts are always enabled before fn(arg) runs. Using callee-saved registers (r12, r13) to pass fn and arg bypasses the normal argument-in-rdi ABI since context_switch's `ret` cannot use the call convention.

### Problems Faced

#### Problem 1 — Shell silent after scheduler switch
**Symptom:** "[boot] Launching shell..." appears but nothing from shell_run.
**Debug:** Added `[tramp] fn=%p` debug — both shell and idle trampolines printed, but shell's kprintf inside shell_run did not.
**Root cause:** The timer ISR preempted kprintf mid-execution (between VGA row update and serial write). The idle task's kprintf ran, corrupting vga_row/vga_col. When shell resumed, VGA writes went to garbled positions, invisible on serial.
**Fix:** Disabled timer-driven preemption. Shell runs cooperatively; timer fires for jiffies only.

#### Problem 2 — `current_task` NULL dereference in schedule()
**Symptom:** Page fault at address 0x14 (= offset of `state` field in `task_t`).
**Root cause:** `sched_start` set `current_task = &bootstrap` (not in run queue). When schedule ran, `current_task->next = NULL` → fault accessing `NULL->state`.
**Fix:** Changed `sched_start` to set `current_task = first` (the actual shell task, which IS in the run queue).

#### Problem 3 — Wrong stack frame register order in task_create
**Root cause:** Initial stack was built in r15-first order, but `context_switch` pops in rbx-first order. Registers were scrambled (r15 got arg instead of rbx).
**Fix:** Rebuilt stack in correct order: `[rbx=0][rbp=0][r12=fn][r13=arg][r14=0][r15=0][trampoline]` matching `pop rbx; pop rbp; pop r12; pop r13; pop r14; pop r15; ret` order.

#### Problem 4 — `sti` race in schedule() before context_switch
**Symptom:** Double-scheduling — timer fired between `sti` and `context_switch`, causing nested schedule calls and corrupted task state.
**Fix:** Removed `sti` from schedule(). Resumed tasks re-enable interrupts via `iretq` (RFLAGS restoration) or via `task_trampoline`'s explicit `sti` for new tasks.

#### Problem 5 — vfs.c strchr undefined
**Root cause:** `vfs_lookup` uses `strchr` but it wasn't in `string.h/c`.
**Fix:** Added `strchr`, `strrchr`, `strcat`, `strncat` to `lib/string.c`.

### Current Boot Output
```
  Lithium Kernel 1.0.0  x86_64

[boot] Version: kernel 1.0.0
[boot] CPU: GDT/IDT/TSS  EFER=0xd01
[boot] PMM: 254 MiB usable RAM
[boot] VMM: paging active, null+guard unmapped
heap: slab allocator ready (7 size classes, 16–1024 bytes)
[boot] Timer: PIT @ 1000 Hz
[boot] Scheduler: round-robin ready
[boot] Keyboard: PS/2 IRQ1 active
VFS: tmpfs root mounted, /dev populated
[boot] Power: ACPI S5 ready (PM1a=0x404 type=0)
[boot] Boot complete; launching shell...

Welcome to lithium OS, kernel ver- 1.0.0.
Type 'help' for a list of commands.
lithium:~# 
```

### What's Needed Next
1. **Phase 13 spinlocks** → re-enable preemptive scheduling
2. **Phase 8 syscalls** → userspace programs
3. **Phase 6 ELF loader** → run user binaries
4. **Phase 11 PCI + virtio-blk** → persistent storage
5. **Phase 10 ext2** → real filesystem
6. **Phase 12 networking** → ping, TCP
---

## 2026-06-22 — Stabilization: Versioning, Keyboard Compatibility, PMM Scan Performance

### Goal
Clean up the VM-ready ISO after QEMU/Hyper-V testing: make version `1.0.0` visible everywhere, keep Linux-style boot logs before clearing into the shell, improve keyboard compatibility, and reduce avoidable PMM allocation scanning.

### What Was Changed

| File | Purpose |
|------|---------|
| `include/version.h` | Single source of truth for `LITHIUM_VERSION = "1.0.0"` |
| `kernel/main.c` | Prints `[boot] Version: kernel 1.0.0`; keeps boot logs visible briefly, then clears before shell |
| `kernel/fs/vfs.c` | `/etc/motd` now prints `Welcome to lithium OS, kernel ver- 1.0.0.` |
| `kernel/shell/shell.c` | `uname -a` and `clear` use the shared version string |
| `drivers/char/ps2kbd.c` | Keyboard decoder starts in auto mode; supports translated set 1 and raw set 2 streams |
| `tests/test_ps2kbd_decode.c` | Added set-1 release, set-2 release, Backspace, and mixed-stream regression coverage |
| `kernel/mm/pmm.c` | Bounds frame scans to highest usable RAM and adds a next-fit allocation hint |
| `Makefile`, `iso/boot/grub/grub.cfg` | GRUB menu now says `Lithium OS 1.0.0` |

### Problems Faced

#### Problem 1 — QEMU keyboard works but Hyper-V previously produced junk
**Evidence:** QEMU typed accurately after the translated set-1 fix. Earlier Hyper-V screenshots showed printable garbage when the driver was forced into raw set 2 while the VM appeared to be delivering translated break bytes.

**Root cause hypothesis:** The keyboard stream differs by VM path. QEMU's default PS/2 path is translated set 1; Hyper-V/VMConnect may expose translated set 1, raw set 2, or extended/break byte behavior depending on VM settings. Hard-forcing either set can make the other VM decode releases as printable characters.

**Fix:** Start the decoder in auto mode. The first meaningful scancode chooses set 1 or set 2, `0xF0` switches to set 2 release handling, high-bit set-1 break bytes are ignored, and keymap lookups are bounds-checked.

**Remaining diagnostic if Hyper-V still differs:** add a tiny scancode trace ring printed via boot option or shell command so the exact bytes from Hyper-V can be compared to QEMU.

#### Problem 2 — Version existed but was easy to miss
**Evidence:** The old banner printed early and scrolled away under memory-map logs. The shell MOTD still said only `Welcome to Lithium OS`.

**Fix:** Keep the boot-log version line and also print the requested MOTD wording after the screen clears.

#### Problem 3 — PMM scanned beyond real RAM
**Evidence:** Existing Phase 2 notes already documented QEMU's high MMIO reserved region pushing `pmm_total_frames` toward 4 GiB even on small VMs.

**Fix:** Calculate `pmm_total_frames` from the highest usable RAM entry only, and keep `pmm_next_hint` so repeated single-frame allocations continue near the last successful allocation instead of rescanning from frame 0 every time.

### Current State

Lithium is currently a bootable x86-64 hobby kernel with:

- Multiboot2 ISO boot through GRUB
- PMM, VMM, slab heap
- PIT timer and cooperative scheduler
- tmpfs/devfs VFS
- VGA + serial console
- PS/2 keyboard input
- ACPI/VM poweroff path
- Interactive shell with core diagnostic commands

### What's Left

1. **Hyper-V keyboard follow-up** — test the latest ISO; if still wrong, implement scancode tracing and tune against real Hyper-V bytes.
2. **Spinlocks / preemption** — required before timer IRQ can schedule safely.
3. **Syscalls + userspace** — needed for actual user programs instead of kernel-built-in shell commands.
4. **ELF loader + init** — boot into PID 1 and user shell.
5. **PCI + virtio-blk + filesystem** — persistent storage.
6. **Networking** — NIC driver, ARP/IPv4/ICMP/UDP/TCP.
7. **Build portability** — current Windows build uses explicit Clang/LLD commands; Makefile still assumes Unix tools for ISO creation.
