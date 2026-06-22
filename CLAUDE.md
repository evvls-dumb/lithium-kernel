# Lithium Kernel

Lithium is a monolithic kernel built from scratch, targeting x86-64 architecture. It aims to be a Linux-class kernel with a proper memory manager, CPU scheduler, driver model, VFS layer, networking stack, and POSIX-compatible syscall interface.

---

## Team

**Team Lithium**

---

## Architecture Decision: Monolithic Kernel

Lithium is monolithic — drivers and subsystems run in kernel space for performance. A clean internal module interface (similar to Linux's `module_init`) keeps subsystems decoupled even though they share an address space.

**Language:** C (core kernel), Assembly (bootstrap, low-level CPU instructions)  
**Target ISA:** x86-64 (AMD64)  
**Toolchain:** GCC cross-compiler (`x86_64-elf-gcc`), NASM/GNU AS, GNU Make  
**Boot protocol:** Multiboot2 (GRUB2 compatible)  
**Build output:** ELF kernel image loaded by GRUB  

---

## Repository Layout

```
lithium/
├── arch/
│   └── x86_64/
│       ├── boot/
│       │   └── entry.asm        ✓ Multiboot2 header, 32→64 bootstrap
│       └── cpu/
│           ├── gdt.h / gdt.c    ✓ GDT (7 entries incl. TSS)
│           ├── gdt.asm          ✓ gdt_load (far-return CS reload), tss_load
│           ├── tss.h / tss.c    ✓ 64-bit TSS
│           ├── idt.h / idt.c    ✓ IDT + PIC remap + C dispatcher
│           ├── idt.asm          ✓ 256 ISR stubs + isr_table[] + isr_common_stub
│           ├── cpuid.h / cpuid.c✓ CPUID feature detection
│           ├── msr.h            ✓ rdmsr/wrmsr, EFER/STAR/LSTAR/FMASK constants
│           └── pvh.h            ✓ Xen PVH hvm_start_info + hvm_memmap_entry
├── kernel/
│   ├── main.c                   ✓ kmain — Phase 0/1/2 boot sequence
│   ├── panic.h / panic.c        ✓ panic() + panic_exception() with register dump
│   ├── kassert.c                ✓ KASSERT() backing implementation
│   ├── mm/
│   │   ├── pmm.h / pmm.c        ✓ Bitmap PMM — alloc/free/contiguous/stats/selftest
│   │   ├── vmm.h / vmm.c        ✓ 4-level paging, map/unmap, guard page, kheap alloc
│   │   └── heap.h / heap.c      ✓ Slab allocator (7 classes) + large alloc via VMM
│   ├── sched/
│   │   ├── task.h / task.c      ✓ struct task, task_create, task_exit, context_switch
│   │   └── sched.h / sched.c    ✓ Round-robin scheduler, sched_start, cooperative yield
│   ├── fs/
│   │   └── vfs.h / vfs.c        ✓ VFS core, tmpfs, devfs, FD table
│   ├── shell/
│   │   └── shell.h / shell.c    ✓ Interactive shell (15 commands, history, colours)
│   ├── proc/                    (Phase 6)
│   ├── ipc/                     (Phase 9)
│   └── syscall/                 (Phase 8)
├── drivers/
│   ├── char/
│   │   ├── vga.h / vga.c        ✓ 80×25 VGA text mode, 16 colours, scroll
│   │   ├── serial.h / serial.c  ✓ COM1 UART 16550 @ 38400 baud
│   │   └── ps2kbd.h / ps2kbd.c  ✓ PS/2 keyboard, IRQ1, scancode→ASCII, circular buf
│   ├── block/                   (Phase 11 — virtio-blk planned)
│   ├── pci/                     (Phase 11 — enumeration planned)
│   └── acpi/                    (Phase 5 — MADT planned)
├── fs/                          (Phase 10)
├── net/                         (Phase 12)
├── lib/
│   ├── string.h / string.c      ✓ memset/cpy/move/cmp, strlen, strcmp, strcpy
│   └── kprintf.h / kprintf.c    ✓ kprintf — VGA + serial, %d %u %x %X %p %s %c
├── include/
│   ├── types.h                  ✓ stdint / stddef / stdbool pull-in
│   ├── io.h                     ✓ outb/inb/outw/inw/outl/inl, io_wait
│   ├── kernel.h                 ✓ KASSERT, ALIGN_UP/DOWN, BIT, MIN, MAX, UNUSED
│   └── multiboot2.h             ✓ MB2 info / tag / mmap structures
├── kernel/irq.h                 ✓ IRQ framework: register, mask, unmask, dispatch
├── kernel/timer.h / timer.c     ✓ PIT 1000 Hz, jiffies, timer_ms()
├── tools/                       (future: mkiso, symbol-map)
├── tests/                       (future: QEMU integration tests)
├── linker.ld                    ✓ Links at 0x100000; exports _kernel_end, _bss_*
├── Makefile                     ✓ make / make iso / make run / make run-serial / make debug
├── CLAUDE.md                    ✓ This file
└── logs.md                      ✓ Development log
```

---

## Roadmap

### Phase 0 — Toolchain & Bootstrap (Week 1–2) ✅ COMPLETE

**Goal:** Boot a "Hello, Kernel!" message in QEMU. Nothing more.

- [x] Set up cross-compiler: `x86_64-elf-gcc`, `x86_64-elf-ld`, `nasm`
- [x] Write Multiboot2 header in NASM (`boot/entry.asm`)
- [x] Set up initial 16 KiB stack in assembly before jumping to C
- [x] Implement `kprintf` over VGA text mode (80×25, port `0xB8000`)
- [x] Write `Makefile` with `make`, `make iso`, `make run` (QEMU) targets
- [x] Boot in QEMU with `-kernel lithium.elf` and see output
- [x] Configure GRUB2 ISO build (`grub-mkrescue`)
- [x] COM1 serial output (secondary debug channel via `-serial stdio`)

**Exit criteria:** `kprintf("Lithium booting...\n")` visible in QEMU. ✓

---

### Phase 1 — CPU Initialization (Week 3–4) ✅ COMPLETE

**Goal:** Full CPU environment before any interrupts fire.

- [x] **GDT** — 7-entry GDT: null, kernel code/data, user code/data, TSS (16-byte system desc)
- [x] **TSS** — 64-bit TSS with RSP0 seeded to kernel stack top
- [x] **IDT** — 256 entries; stubs 0–31 (exceptions), 32–47 (IRQs), 48–255 (software)
- [x] **ISR stubs** — NASM macro (`ISR_NOERR`/`ISR_ERR`) normalises stack, `isr_table[256]` jump table
- [x] **Exception handlers** — all CPU exceptions → `panic_exception()` with full register dump
- [x] **PIC 8259** — remapped (0x20-0x2F), all IRQs masked until Phase 5
- [x] **CPUID** — APIC, x2APIC, SSE/SSE2/AVX/AVX2, NX, SYSCALL, 1 GiB pages
- [x] **MSR** — `EFER.NXE` (NX), `EFER.SCE` (SYSCALL), `STAR`/`LSTAR`/`FMASK` wired up

**Exit criteria:** Division-by-zero triggers a formatted kernel panic with register dump. ✓ (uncomment test in `kmain`)

---

### Phase 2 — Physical Memory Manager (Week 5–6) ✅ COMPLETE

**Goal:** Know every byte of RAM and be able to hand out 4 KiB frames.

- [x] Parse Multiboot2 memory map to find usable regions
- [x] Parse PVH/QEMU E820-style memory map (hvm_start_info)
- [x] Build a **bitmap allocator** over all physical frames (128 KiB bitmap, 4 GiB max)
- [x] Implement `pmm_alloc_frame()` / `pmm_free_frame()`
- [x] Mark reserved regions (kernel image 0x100000–_kernel_end, low 1 MiB) as used
- [x] Add `pmm_alloc_contiguous(n)` for DMA-friendly multi-frame allocations
- [x] Expose `pmm_stats()` for diagnostics

**Exit criteria:** Kernel prints total RAM detected and successfully allocates/frees 1000 frames in a boot-time self-test. ✓

**Verified output (QEMU 256 MiB):**
- 254 MiB usable (65 197 free frames)
- 1 000-frame alloc/free cycle passed
- 16-frame contiguous alloc at `0x133000` passed

---

### Phase 3 — Virtual Memory Manager & Paging (Week 7–9) ✅ COMPLETE

**Goal:** 64-bit 4-level paging, kernel mapped high, per-process page tables.

- [x] Bootstrap PML4/PDPT/PD/PT in assembly before C entry (2 MiB huge pages)
- [x] **Higher-half kernel alias** — kernel mapped at `0xFFFFFFFF80000000` via 2 MiB huge page
- [x] `vmm_map_page(pml4, virt, phys, flags)` / `vmm_unmap_page()` with TLB flush
- [x] `vmm_create_address_space()` — new PML4 with kernel half shared
- [x] **Page fault handler** — prints CR2, distinguishes not-present / protection / user / kernel
- [x] **Guard page** — page below kernel stack unmapped (`0x10B000`)
- [x] Null page (`0x0–0xFFF`) unmapped after VMM init
- [x] `vmm_alloc_kernel(size)` — bump-pointer virtual address allocator
- [x] `vmm_get_physical(pml4, virt)` — page-table walk (handles 2 MiB huge pages)
- [x] Fine-grained 4 KiB pages for first 2 MiB; 2 MiB huge pages for 2 MiB–1 GiB

**Exit criteria:** Kernel runs in higher half; accessing a null pointer triggers a clean page-fault panic. ✓

**Verified (QEMU):**
- Identity map: `virt 0x102b20 → phys 0x102b20` ✓
- Higher-half: `virt 0xFFFFFFFF80102b20 → phys 0x102b20` ✓
- Null page unmapped, guard page at `0x10B000` unmapped ✓
- `vmm_map_page` + `vmm_unmap_page` working ✓
- Null pointer → `#PF: not-present | read | kernel-mode @ 0x0` ✓

---

### Phase 4 — Kernel Heap (Week 10) ✅ COMPLETE

**Goal:** `kmalloc` / `kfree` that work correctly under repeated use.

- [x] Implement a **slab allocator** for fixed-size objects (16, 32, 64, 128, 256, 512, 1024 B)
- [x] Large allocations (>1 KiB) backed by VMM pages from the kernel heap
- [x] Implement `kmalloc(size)`, `kfree(ptr)`, `krealloc(ptr, size)`, `kzalloc(size)`
- [x] Canary checking in large alloc header (`0xFEEDFACEDEADBEEF`)
- [x] `heap_stats()` for diagnostics

**Exit criteria:** Heap allocates VFS vnodes, task structs, task stacks throughout the kernel.

---

### Phase 5 — Interrupt Controller & Timers (Week 11–12) ✅ COMPLETE

**Goal:** Preemptive interrupts firing at a stable rate.

- [x] **PIC 8259** — remapped (0x20–0x2F), all IRQs masked, selective unmask via `irq_unmask()`
- [x] **IRQ framework** — `irq_register(n, handler)`, `irq_mask/unmask()`, dispatch in `isr_handler`
- [x] **PIT 8253** — 1000 Hz, jiffies counter
- [x] `timer_ms()` — milliseconds since boot
- [x] `jiffies` — tick counter (cooperative scheduling, no preemption until Phase 13)

**Note:** Preemptive scheduling disabled until Phase 13 spinlocks — timer fires for jiffies only.

**Exit criteria:** `uptime` command shows elapsed time correctly. ✓

---

### Phase 6 — Process & Thread Management (Week 13–15) ✅ PARTIAL

**Goal:** Multiple kernel threads running concurrently; first userspace process.

- [x] `struct task` — PID, state, rsp, cr3, stack_base, ticks, name
- [x] `task_create(name, fn, arg)` — allocates 8 KiB kernel stack, sets up initial frame
- [x] `task_exit(code)` — marks DEAD, calls schedule
- [x] `context_switch(old_rsp, new_rsp)` — saves/restores r15-r12, rbp, rbx via stack
- [x] `task_trampoline` — proper fn(arg) dispatch with callee-saved registers
- [ ] `task_fork()` — CoW page table clone (Phase 6 remainder)
- [ ] ELF64 loader / userspace (Phase 6 remainder)

**Exit criteria (partial):** kernel threads schedulable, context switch tested ✓

---

### Phase 7 — CPU Scheduler (Week 16–18) ✅ PARTIAL

**Goal:** Fair, preemptive, multi-level scheduler.

- [x] **Run queue** — circular singly-linked list of runnable tasks
- [x] **Round-Robin** — `schedule()` advances to next READY task
- [x] `sched_init()`, `sched_add()`, `sched_remove()`, `sched_yield()`
- [x] `sched_start(first)` — bootstrap: creates idle task, does first context switch
- [ ] Preemptive scheduling from timer IRQ (disabled until Phase 13 spinlocks)
- [ ] MLFQ priority levels (Phase 7 remainder)

**Note:** Cooperative mode active. `sched_yield()` works; preemption needs Phase 13 spinlocks.

---

### Phase 8 — Syscall Interface (Week 19–20)

**Goal:** Userspace can make kernel calls safely.

- [ ] `SYSCALL`/`SYSRET` entry point in NASM; save/restore user registers
- [ ] Syscall dispatch table (function pointer array indexed by `rax`)
- [ ] Implement first syscalls:
  - `SYS_exit` (60)
  - `SYS_write` (1) — write to fd 1/2 via VGA early console
  - `SYS_read` (0)
  - `SYS_open`, `SYS_close` (2, 3) — stub returning `-ENOSYS` until VFS ready
  - `SYS_brk` (12) — extend user heap
  - `SYS_mmap` / `SYS_munmap` (9, 11)
  - `SYS_getpid` (39), `SYS_fork` (57), `SYS_execve` (59)
  - `SYS_waitpid` (61)
- [ ] User-pointer validation — reject any ptr not in user address range
- [ ] `copy_from_user` / `copy_to_user` with fault handling

**Exit criteria:** Userspace `write(1, "hi\n", 3)` prints to console via syscall.

---

### Phase 9 — IPC & Signals (Week 21–23)

**Goal:** Processes can communicate and be signaled.

- [ ] **Signals** — `struct sigaction`, pending signal bitmask, delivery on return-to-user
  - Implement: `SIGKILL`, `SIGTERM`, `SIGSEGV`, `SIGINT`, `SIGCHLD`, `SIGUSR1/2`
  - `kill(pid, sig)`, `sigaction()`, `sigprocmask()`, `sigsuspend()`
  - Signal stack frame setup (push ucontext, jump to handler, `sigreturn`)
- [ ] **Pipes** — anonymous `pipe()`, circular kernel buffer, blocking read/write
- [ ] **Anonymous shared memory** — `mmap(MAP_SHARED|MAP_ANONYMOUS)`
- [ ] **Futex** — `futex(FUTEX_WAIT / FUTEX_WAKE)` for user-space locking
- [ ] **Message queues** (POSIX mq) — bonus

**Exit criteria:** Parent/child communicate over a pipe; `Ctrl+C` sends `SIGINT` and terminates a loop.

---

### Phase 10 — Virtual Filesystem (Week 24–27) ✅ PARTIAL

**Goal:** A clean VFS layer with at least one real filesystem.

- [x] **VFS core** — `vnode_t`, `vfs_ops_t`, `file_t`, FD table (`VFS_MAX_FDS=32`)
- [x] `vfs_open`, `vfs_close`, `vfs_read`, `vfs_write`, `vfs_readdir`, `vfs_lookup`
- [x] **tmpfs** — in-memory files (`tmpfs_file_t`, dynamic buffer), directories
- [x] **devfs** — `/dev/null`, `/dev/zero`, `/dev/tty` under `/dev`
- [x] Pre-populated: `/etc/hostname`, `/etc/motd`, `/dev/*`
- [ ] **ext2** — (upcoming, needs virtio-blk)
- [ ] `procfs` — (upcoming)

**Exit criteria (partial):** `cat /etc/hostname`, `cat /etc/motd`, `ls /`, `ls /dev` all work ✓

---

### Phase 11 — Device Drivers (Week 28–32) ✅ PARTIAL

**Goal:** Keyboard input, storage, and a real display.

- [x] **Serial UART (16550)** — COM1 @ 38400 baud, dual-output with VGA
- [x] **PS/2 Keyboard** — IRQ1, auto-detects translated set 1 vs raw set 2, US QWERTY, shift/caps/ctrl, circular buffer 256B
- [x] **VGA text mode** — 80×25, scrolling, 16 colours, tab/CR/LF
- [ ] **PCI bus** — (upcoming)
- [ ] **ATA/IDE / virtio-blk** — (upcoming)
- [ ] **VGA framebuffer** — (upcoming, VESA/GOP)

**Exit criteria (partial):** Keyboard input working in shell; VGA text + serial dual output ✓

---

### Phase 12 — Networking Stack (Week 33–40)

**Goal:** Ping works; TCP socket connects to the internet.

- [ ] **sk_buff** — socket buffer chain structure (like Linux's `sk_buff`)
- [ ] **Ethernet layer** — frame TX/RX, ARP table, ARP request/reply
- [ ] **IPv4** — packet RX demux, TX routing, checksum, fragmentation
- [ ] **ICMP** — echo request/reply (`ping`)
- [ ] **UDP** — stateless datagram send/receive
- [ ] **TCP** — 3-way handshake, seq/ack tracking, retransmit timer, sliding window, FIN teardown
- [ ] **BSD socket API** — `socket()`, `bind()`, `connect()`, `listen()`, `accept()`, `send()`, `recv()`, `select()`
- [ ] **DHCP client** (userspace or kernel) — obtain IP at boot
- [ ] **DNS resolver** stub

**Exit criteria:** `ping 8.8.8.8` works in QEMU; a simple HTTP GET over TCP retrieves a page.

---

### Phase 13 — SMP (Symmetric Multi-Processing) (Week 41–45)

**Goal:** All QEMU CPUs active and running tasks.

- [ ] Parse MADT for additional APIC IDs (Application Processors)
- [ ] AP startup sequence — send INIT/SIPI IPIs, trampoline code in low memory
- [ ] Per-CPU data (`struct cpu_info`) — accessed via `gs:0` on each CPU
- [ ] Per-CPU run queues — each CPU schedules independently
- [ ] **Spinlocks** — `lock xchg` based; used for all shared structures
- [ ] **Mutex / semaphore** — sleeping locks using wait queues
- [ ] **RCU** (Read-Copy-Update) — lockless reads for hot-path structures (stretch)
- [ ] **IPI** — inter-processor interrupt for TLB shootdown, task migration
- [ ] Load balancer — migrate tasks between CPU run queues to equalize load
- [ ] `sched_setaffinity()` — pin tasks to specific CPUs

**Exit criteria:** 4-CPU QEMU run shows all CPUs executing tasks; SMP spinlock stress test passes.

---

### Phase 14 — Security & Hardening (Week 46–48)

**Goal:** Kernel resists common exploitation techniques.

- [ ] **SMEP/SMAP** — enable in CR4; kernel cannot execute/access user pages
- [ ] **KASLR** — randomize kernel load address at boot (requires PIE kernel)
- [ ] **Stack canaries** — `__stack_chk_guard` in GCC (`-fstack-protector-strong`)
- [ ] **NX stacks/heap** — all non-code mappings marked `NX` in page tables
- [ ] **Capabilities** — coarse privilege model (replace root/non-root binary)
- [ ] **Seccomp-like filter** — per-process syscall allowlist
- [ ] **ASLR** — randomize user stack, heap, mmap base
- [ ] Audit trail: log security-sensitive syscalls (`execve`, `mount`, `open` with `O_WRONLY`)

**Exit criteria:** Kernel compiled with all mitigations; a test ROP chain is blocked by SMEP.

---

### Phase 15 — Userland & Shell (Week 49–52)

**Goal:** A bootable system with a working shell prompt.

- [ ] **libk / libc stub** — minimal C runtime: `syscall()`, `printf`, `malloc`, `string.h`
- [ ] **`init`** — PID 1; mounts filesystems, spawns shell
- [ ] **`sh`** — minimal POSIX shell: `cd`, `exec`, pipes `|`, redirection `>/<`, background `&`
- [ ] **Coreutils subset**: `ls`, `cat`, `echo`, `mkdir`, `rm`, `cp`, `mv`, `ps`, `kill`, `sleep`
- [ ] **`/etc/passwd`** — user database; `login` program
- [ ] Port **musl libc** (stretch goal) — replace libk stub with a real libc

**Exit criteria:** System boots to a `lithium$ ` shell prompt; user can run `ls /`, `cat /etc/hostname`, `ps`, and pipe commands together.

---

## Key Milestones Summary

| Milestone | Phase | Status |
|-----------|-------|--------|
| "Hello Kernel" in QEMU | 0 | ✅ DONE |
| CPU rings + panic handler | 1 | ✅ DONE |
| Physical memory allocator | 2 | ✅ DONE |
| 64-bit paging, higher half | 3 | ✅ DONE |
| kmalloc / kfree working | 4 | ✅ DONE |
| Timer interrupts at 1 kHz | 5 | ✅ DONE |
| Kernel threads + context switch | 6 | ✅ DONE (cooperative) |
| Round-robin scheduler | 7 | ✅ DONE (cooperative) |
| Syscall interface | 8 | ⏳ Next |
| Signals + pipes | 9 | ⏳ Planned |
| VFS + tmpfs + devfs | 10 | ✅ DONE (partial) |
| PS/2 keyboard + VGA | 11 | ✅ DONE (partial) |
| Networking | 12 | ⏳ Planned |
| SMP + spinlocks (enables preemption) | 13 | ⏳ Planned |
| Security hardening | 14 | ⏳ Planned |
| Interactive shell (15 commands) | 15 | ✅ DONE |

---

## Current Stabilization Snapshot (2026-06-22)

### Where We Are

Lithium boots from a GRUB Multiboot2 ISO into a monolithic x86-64 kernel with paging, PMM/VMM, slab heap, PIT timer, cooperative scheduler, tmpfs/devfs VFS, VGA + serial console, PS/2 keyboard input, ACPI/VM poweroff path, and an interactive shell.

Current user-visible version:

```
Welcome to lithium OS, kernel ver- 1.0.0.
```

Boot now shows diagnostic logs first, prints `[boot] Version: kernel 1.0.0`, waits briefly, clears the screen, and enters the shell.

### Recent Stability / Performance Work

| Area | Change |
|------|--------|
| Versioning | `include/version.h` is the single source for `1.0.0`; boot logs, MOTD, `uname`, and GRUB title use it |
| Keyboard | Decoder starts in auto mode and supports both translated set 1 and raw set 2 scancode streams |
| Keyboard safety | Release/break bytes are ignored instead of becoming printable garbage |
| PMM performance | Frame scan range now follows highest usable RAM, not high MMIO holes; single-frame allocation keeps a next-fit hint |
| Boot UX | Logs remain visible briefly, then VGA clears before the shell prompt |
| Power | `shutdown` / `poweroff` try ACPI S5 first, then common VM fallback ports |

### Known Gaps

1. Hyper-V keyboard still needs a fresh real-VM test with the latest ISO. QEMU now works; if Hyper-V still differs, capture exact scancode bytes next.
2. Hyper-V host-side "Shut Down" may require VMBus / integration services. In-guest `shutdown` and `poweroff` are the supported path for now.
3. Scheduling is cooperative until Phase 13 spinlocks make preemption safe around VGA, serial, VFS, and allocator state.
4. There is no userspace yet: syscalls, ELF loading, `init`, and process isolation are still ahead.
5. Storage/networking are not implemented yet: PCI, virtio-blk, ext2, virtio-net/e1000, IPv4, and TCP remain future phases.

### Best Next Improvements

1. Add a kernel scancode trace ring and a `kbdtrace` shell/boot option for Hyper-V diagnosis.
2. Implement spinlocks and re-enable timer-driven preemption.
3. Add a proper syscall entry path and first user-mode smoke program.
4. Add PCI enumeration, then virtio-blk, then ext2 or a simpler initrd-backed filesystem.
5. Split tmpfs/devfs from `vfs.c` once storage work starts.

---

## Development Environment

### Cross-compiler setup (one-time)

On macOS with Homebrew:
```bash
brew install x86_64-elf-gcc nasm qemu xorriso
# grub-mkrescue: brew install --cask grub  (or build from source for macOS)
```

On Ubuntu/Debian:
```bash
sudo apt install gcc-x86-64-linux-gnu nasm qemu-system-x86 grub-common xorriso
# Or build a bare-metal cross-compiler: https://wiki.osdev.org/GCC_Cross-Compiler
```

### Build & run

```bash
# Build the kernel ELF
make

# Build a bootable ISO (needs grub-mkrescue + xorriso)
make iso

# Run directly in QEMU (no ISO needed — fastest iteration loop)
make run

# Run with COM1 serial port forwarded to your terminal (recommended for debugging)
make run-serial

# Attach GDB (in a second terminal after `make debug`)
make debug
gdb lithium.elf -ex "target remote :1234" -ex "layout src"
```

### Verify the build works

```bash
# The ELF should be ~50-200 KB and have a .multiboot2 section
x86_64-elf-objdump -h lithium.elf | grep -E "multiboot|text|data|bss"

# Check symbols
x86_64-elf-nm lithium.elf | grep -E "kmain|_start|gdt_init|idt_init"
```

Required host tools: `x86_64-elf-gcc`, `x86_64-elf-ld`, `nasm`, `make`, `qemu-system-x86_64`, `grub-mkrescue`, `xorriso`, `gdb`

---

## Coding Standards

- C11 (`-std=c11`), `-Wall -Wextra -Werror`, `-ffreestanding`, `-nostdlib`
- No floating point in kernel (`-mno-sse -mno-mmx` in Makefile — SSE state is not saved on interrupt entry)
- `KASSERT(cond)` macro halts with file:line on failure in debug builds
- No dynamic memory in interrupt context — pre-allocate or use per-CPU slab caches (Phase 4+)
- Spinlocks held for < 1 µs; anything longer needs a sleeping lock (Phase 13+)
- All user pointers validated before dereference — `copy_from_user`/`copy_to_user` (Phase 8+)
- New subsystem = new directory; sources auto-discovered by `find` in Makefile
- Include paths are always from repo root (`-I.`): write `#include "lib/kprintf.h"` not `"../kprintf.h"`
- No comments explaining WHAT code does — only WHY (hidden constraint, non-obvious invariant)

---

## References & Prior Art

- OSDev Wiki — https://wiki.osdev.org
- Intel Software Developer Manual (SDM) — Vol 1-3
- AMD64 Architecture Programmer's Manual
- Linux Kernel source — for design patterns (not for copying code)
- xv6 (MIT) — clean teaching kernel, good reference for early phases
- SerenityOS — modern from-scratch OS, good driver patterns
- Multiboot2 specification — https://www.gnu.org/software/grub/manual/multiboot2/
