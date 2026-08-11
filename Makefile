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
# Clang/LLD: cross-compile to bare-metal x86-64 ELF.
# GNU make has built-in defaults for CC/LD, so replace only those defaults;
# explicit command-line overrides still win.
ifeq ($(OS),Windows_NT)
LLVM_BIN   := C:/Program Files/LLVM/bin
DEFAULT_CC := "$(LLVM_BIN)/clang.exe"
DEFAULT_LD := "$(LLVM_BIN)/ld.lld.exe"
else
DEFAULT_CC := clang
DEFAULT_LD := ld.lld
endif

ifeq ($(origin CC),default)
CC := $(DEFAULT_CC)
endif

ifeq ($(origin LD),default)
LD := $(DEFAULT_LD)
endif

ifeq ($(origin AS),default)
AS := nasm
endif

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
    -MMD \
    -MP \
    -I. \
    -I./include

# ── Assembler flags ───────────────────────────────────────────
ASFLAGS := -f elf64

# ── Linker flags ──────────────────────────────────────────────
LDFLAGS := -T linker.ld --nostdlib

# ── Output artefacts ─────────────────────────────────────────
KERNEL  := lithium.elf
ISO     := lithium.iso
USER_SMOKE_ELF := user/smoke.elf
USER_EXECER_ELF := user/execer.elf
USER_INIT_ELF := user/init.elf
USER_SH_ELF := user/sh.elf
USER_ECHO_ELF := user/echo.elf
USER_LS_ELF := user/ls.elf
USER_CAT_ELF := user/cat.elf
USER_PS_ELF := user/ps.elf

USER_CFLAGS := \
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
    -I.

# ── Source discovery ─────────────────────────────────────────
C_SRCS   := $(shell find . -name '*.c'   ! -path './tools/*' ! -path './tests/*' ! -path './user/*')
ASM_SRCS := $(shell find . -name '*.asm' ! -path './tools/*' ! -path './tests/*' ! -path './user/*')

C_OBJS   := $(C_SRCS:.c=.o)
ASM_OBJS := $(ASM_SRCS:.asm=.o)

OBJS := $(ASM_OBJS) $(C_OBJS)
DEPS := $(C_OBJS:.o=.d)

# ── Default target ────────────────────────────────────────────
.PHONY: all
all: $(KERNEL)

kernel/proc/user_smoke_elf.o: $(USER_SMOKE_ELF)
kernel/proc/user_execer_elf.o: $(USER_EXECER_ELF)
kernel/proc/user_init_elf.o: $(USER_INIT_ELF)
kernel/proc/user_sh_elf.o: $(USER_SH_ELF)
kernel/proc/user_echo_elf.o: $(USER_ECHO_ELF)
kernel/proc/user_ls_elf.o: $(USER_LS_ELF)
kernel/proc/user_cat_elf.o: $(USER_CAT_ELF)
kernel/proc/user_ps_elf.o: $(USER_PS_ELF)

user/sh.elf: user/lib/crt0.o user/lib/ulib.o user/sh.o user/linker.ld
	$(LD) -T user/linker.ld --nostdlib -o $@ user/lib/crt0.o user/lib/ulib.o user/sh.o
	@echo "[ULD] $@"

user/echo.elf: user/lib/crt0.o user/lib/ulib.o user/echo.o user/linker.ld
	$(LD) -T user/linker.ld --nostdlib -o $@ user/lib/crt0.o user/lib/ulib.o user/echo.o
	@echo "[ULD] $@"

user/ls.elf: user/lib/crt0.o user/lib/ulib.o user/ls.o user/linker.ld
	$(LD) -T user/linker.ld --nostdlib -o $@ user/lib/crt0.o user/lib/ulib.o user/ls.o
	@echo "[ULD] $@"

user/cat.elf: user/lib/crt0.o user/lib/ulib.o user/cat.o user/linker.ld
	$(LD) -T user/linker.ld --nostdlib -o $@ user/lib/crt0.o user/lib/ulib.o user/cat.o
	@echo "[ULD] $@"

user/ps.elf: user/lib/crt0.o user/lib/ulib.o user/ps.o user/linker.ld
	$(LD) -T user/linker.ld --nostdlib -o $@ user/lib/crt0.o user/lib/ulib.o user/ps.o
	@echo "[ULD] $@"

user/%.elf: user/%.o user/linker.ld
	$(LD) -T user/linker.ld --nostdlib -o $@ $<
	@echo "[ULD] $@"

user/%.o: user/%.asm
	@echo "[UAS] $<"
	@$(AS) $(ASFLAGS) -o $@ $<

user/lib/%.o: user/lib/%.asm
	@echo "[UAS] $<"
	@$(AS) $(ASFLAGS) -o $@ $<

user/%.o: user/%.c
	@echo "[UCC] $<"
	@$(CC) $(USER_CFLAGS) -c -o $@ $<

user/lib/%.o: user/lib/%.c
	@echo "[UCC] $<"
	@$(CC) $(USER_CFLAGS) -c -o $@ $<

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
	@$(shell find . \( -name '*.o' -o -name '*.d' \) -exec rm -f {} \;)
	@rm -f $(KERNEL) $(ISO)
	@rm -rf iso
	@echo "[CLN] done"

-include $(DEPS)
