# =============================================================
# Lithium Kernel — top-level Makefile
# =============================================================
#
# Toolchain (Windows + WSL-compatible):
#   Primary:  Clang/LLD (LLVM) targeting x86_64-elf  — works on Windows natively
#   Fallback: x86_64-elf-gcc + x86_64-elf-ld         — set CC/LD env vars
#   Assembler: nasm (Windows or WSL)
#
# Targets:
#   make            — build lithium.elf
#   make iso        — build bootable lithium.iso (needs grub-mkrescue in WSL)
#   make run        — boot kernel directly in QEMU
#   make run-serial — same + forward COM1 to stdout
#   make debug      — start QEMU with GDB stub on :1234, paused
#   make clean      — remove build artefacts

# ── Toolchain ─────────────────────────────────────────────────
# Clang/LLD: cross-compile to bare-metal x86-64 ELF on Windows.
# Force := so GNU make's built-in AS/CC defaults don't win.
LLVM_BIN := C:/Program Files/LLVM/bin
CC       := "$(LLVM_BIN)/clang.exe"
LD       := "$(LLVM_BIN)/ld.lld.exe"
AS       := nasm

CLANG_TARGET := --target=x86_64-elf

# ── Compiler flags ────────────────────────────────────────────
CFLAGS  := \
    $(CLANG_TARGET) \
    -std=c11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pic \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -Wall \
    -Wextra \
    -Werror \
    -I. \
    -I./include

# ── Assembler flags ───────────────────────────────────────────
ASFLAGS := -f elf64

# ── Linker flags ──────────────────────────────────────────────
LDFLAGS := -T linker.ld --nostdlib

# ── Output artefacts ─────────────────────────────────────────
KERNEL  := lithium.elf
ISO     := lithium.iso

# ── Source discovery ─────────────────────────────────────────
C_SRCS   := $(shell find . -name '*.c'   ! -path './tools/*' ! -path './tests/*')
ASM_SRCS := $(shell find . -name '*.asm' ! -path './tools/*' ! -path './tests/*')

C_OBJS   := $(C_SRCS:.c=.o)
ASM_OBJS := $(ASM_SRCS:.asm=.o)

OBJS := $(ASM_OBJS) $(C_OBJS)

# ── Default target ────────────────────────────────────────────
.PHONY: all
all: $(KERNEL)

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[LD]  $@"

# ── Compilation rules ─────────────────────────────────────────
%.o: %.c
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.asm
	@echo "[AS]  $<"
	@$(AS) $(ASFLAGS) -o $@ $<

# ── ISO build ────────────────────────────────────────────────
.PHONY: iso
iso: $(KERNEL)
	@mkdir -p iso/boot/grub
	@cp $(KERNEL) iso/boot/lithium.elf
	@printf 'set timeout=0\nset default=0\nmenuentry "Lithium OS 1.0.0" {\n    multiboot2 /boot/lithium.elf\n    boot\n}\n' \
	    > iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso 2>/dev/null
	@echo "[ISO] $(ISO)"

# ── QEMU run targets ─────────────────────────────────────────
QEMU_ARGS := \
    -m 256M \
    -no-reboot \
    -no-shutdown \
    -display sdl

.PHONY: run
run: $(KERNEL)
	qemu-system-x86_64 -kernel $(KERNEL) $(QEMU_ARGS)

.PHONY: run-serial
run-serial: $(KERNEL)
	qemu-system-x86_64 -kernel $(KERNEL) $(QEMU_ARGS) -serial stdio

.PHONY: run-iso
run-iso: iso
	qemu-system-x86_64 -cdrom $(ISO) $(QEMU_ARGS) -serial stdio

# ── GDB debug stub (paused at entry, waiting for `gdb lithium.elf`) ──
.PHONY: debug
debug: $(KERNEL)
	qemu-system-x86_64 -kernel $(KERNEL) $(QEMU_ARGS) -serial stdio -s -S

# ── Clean ─────────────────────────────────────────────────────
.PHONY: clean
clean:
	@$(shell find . -name '*.o' -exec rm -f {} \;)
	@rm -f $(KERNEL) $(ISO)
	@rm -rf iso
	@echo "[CLN] done"
