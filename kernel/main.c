#include "../include/kernel.h"
#include "../include/version.h"
#include "../drivers/char/vga.h"
#include "../drivers/char/serial.h"
#include "../drivers/char/ps2kbd.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../arch/x86_64/cpu/gdt.h"
#include "../arch/x86_64/cpu/tss.h"
#include "../arch/x86_64/cpu/idt.h"
#include "../arch/x86_64/cpu/cpuid.h"
#include "../arch/x86_64/cpu/msr.h"
#include "panic.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "timer.h"
#include "sched/sched.h"
#include "sched/task.h"
#include "fs/vfs.h"
#include "syscall/syscall.h"
#include "power/power.h"

#define MB2_BOOTLOADER_MAGIC 0x36D76289u
#define MB1_BOOTLOADER_MAGIC 0x2BADB002u
#define PVH_BOOTLOADER_MAGIC 0x336ec578u

extern char _kernel_end[];
extern char stack_top[];
extern const uint8_t user_smoke_elf_start[];
extern const uint8_t user_smoke_elf_end[];
extern const uint8_t user_execer_elf_start[];
extern const uint8_t user_execer_elf_end[];
extern const uint8_t user_init_elf_start[];
extern const uint8_t user_init_elf_end[];
extern const uint8_t user_sh_elf_start[];
extern const uint8_t user_sh_elf_end[];
extern const uint8_t user_echo_elf_start[];
extern const uint8_t user_echo_elf_end[];
extern const uint8_t user_ls_elf_start[];
extern const uint8_t user_ls_elf_end[];
extern const uint8_t user_cat_elf_start[];
extern const uint8_t user_cat_elf_end[];
extern const uint8_t user_ps_elf_start[];
extern const uint8_t user_ps_elf_end[];

void kmain(uint32_t mb2_magic, uint32_t mb2_info_phys) {

    /* ── Phase 0 ─────────────────────────────────────────── */
    vga_init();
    serial_init();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("  _     _ _   _     _\n");
    kprintf(" | |   (_) |_| |__ (_)_   _ _ __ ___\n");
    kprintf(" | |   | | __| '_ \\| | | | | '_ ` _ \\\n");
    kprintf(" | |___| | |_| | | | | |_| | | | | | |\n");
    kprintf(" |_____|_|\\__|_| |_|_|\\__,_|_| |_| |_|\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("  Lithium Kernel %s  x86_64\n\n", LITHIUM_VERSION);
    kprintf("[boot] Version: kernel %s\n", LITHIUM_VERSION);

    /* ── Phase 1: CPU ────────────────────────────────────── */
    tss_init(stack_top);
    gdt_init();
    idt_init();
    cpuid_init();

    uint64_t efer = rdmsr(MSR_EFER);
    const cpuid_features_t *f = cpuid_features();
    if (f->has_nx) efer |= EFER_NXE;
    wrmsr(MSR_EFER, efer);
    syscall_init();
    kprintf("[boot] CPU: GDT/IDT/TSS  EFER=0x%lx\n", rdmsr(MSR_EFER));

    /* ── Phase 2: PMM ────────────────────────────────────── */
    pmm_init(mb2_magic, mb2_info_phys);
    kprintf("[boot] PMM: %lu MiB usable RAM\n",
            pmm_free_bytes() / (1024*1024));

    /* ── Phase 3: VMM ────────────────────────────────────── */
    vmm_init();
    kprintf("[boot] VMM: paging active, null+guard unmapped\n");

    /* ── Phase 4: Heap ───────────────────────────────────── */
    heap_init();

    /* ── Phase 5: Timer ──────────────────────────────────── */
    __asm__ volatile ("sti");
    timer_init(1000);    /* 1 KHz — 1 ms tick */
    kprintf("[boot] Timer: PIT @ 1000 Hz\n");

    /* ── Phase 6/7: Scheduler ───────────────────────────── */
    sched_init();
    kprintf("[boot] Scheduler: round-robin ready\n");

    /* ── PS/2 Keyboard ───────────────────────────────────── */
    kbd_init();
    kprintf("[boot] Keyboard: PS/2 IRQ1 active\n");

    /* ── Phase 10: VFS ───────────────────────────────────── */
    vfs_init();
    KASSERT(vfs_mkdir("/bin") == 0);
    KASSERT(vfs_create_file(
        "/bin/smoke",
        user_smoke_elf_start,
        (size_t)(user_smoke_elf_end - user_smoke_elf_start)) == 0);
    KASSERT(vfs_create_file(
        "/bin/init",
        user_init_elf_start,
        (size_t)(user_init_elf_end - user_init_elf_start)) == 0);
    KASSERT(vfs_create_file(
        "/bin/sh",
        user_sh_elf_start,
        (size_t)(user_sh_elf_end - user_sh_elf_start)) == 0);
    KASSERT(vfs_create_file(
        "/bin/echo",
        user_echo_elf_start,
        (size_t)(user_echo_elf_end - user_echo_elf_start)) == 0);
    KASSERT(vfs_create_file(
        "/bin/ls",
        user_ls_elf_start,
        (size_t)(user_ls_elf_end - user_ls_elf_start)) == 0);
    KASSERT(vfs_create_file(
        "/bin/cat",
        user_cat_elf_start,
        (size_t)(user_cat_elf_end - user_cat_elf_start)) == 0);
    KASSERT(vfs_create_file(
        "/bin/ps",
        user_ps_elf_start,
        (size_t)(user_ps_elf_end - user_ps_elf_start)) == 0);

    /* ── Power management ────────────────────────────────── */
    power_init(mb2_magic, mb2_info_phys);

    kprintf("[boot] Boot complete; launching init...\n");
    timer_sleep_ms(1500);
    vga_clear();

    task_t *init = task_create_user_elf(
        "init-loader",
        user_execer_elf_start,
        (size_t)(user_execer_elf_end - user_execer_elf_start));
    KASSERT(init != NULL);

    sched_start(init);
}
