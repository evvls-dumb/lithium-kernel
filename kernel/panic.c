#include "panic.h"
#include "../lib/kprintf.h"
#include "../drivers/char/vga.h"

static const char *exception_names[32] = {
    [0]  = "Divide-by-Zero (#DE)",
    [1]  = "Debug (#DB)",
    [2]  = "Non-Maskable Interrupt",
    [3]  = "Breakpoint (#BP)",
    [4]  = "Overflow (#OF)",
    [5]  = "Bound Range Exceeded (#BR)",
    [6]  = "Invalid Opcode (#UD)",
    [7]  = "Device Not Available (#NM)",
    [8]  = "Double Fault (#DF)",
    [9]  = "Coprocessor Segment Overrun",
    [10] = "Invalid TSS (#TS)",
    [11] = "Segment Not Present (#NP)",
    [12] = "Stack Segment Fault (#SS)",
    [13] = "General Protection Fault (#GP)",
    [14] = "Page Fault (#PF)",
    [15] = "Reserved",
    [16] = "x87 FP Exception (#MF)",
    [17] = "Alignment Check (#AC)",
    [18] = "Machine Check (#MC)",
    [19] = "SIMD FP Exception (#XM)",
    [20] = "Virtualisation Exception (#VE)",
    [21] = "Control Protection (#CP)",
    [22] = "Reserved",
    [23] = "Reserved",
    [24] = "Reserved",
    [25] = "Reserved",
    [26] = "Reserved",
    [27] = "Reserved",
    [28] = "Hypervisor Injection (#HV)",
    [29] = "VMM Communication (#VC)",
    [30] = "Security Exception (#SX)",
    [31] = "Reserved",
};

void panic(const char *msg) {
    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n[KERNEL PANIC] %s\n", msg);
    kprintf("System halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

void panic_exception(interrupt_frame_t *frame) {
    const char *name = (frame->vector < 32)
        ? exception_names[frame->vector]
        : "Unknown Vector";

    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n");
    kprintf("======================================================\n");
    kprintf("  KERNEL PANIC — Exception #%lu: %s\n",
            frame->vector, name);
    kprintf("======================================================\n");

    /* #PF (vector 14): print CR2 (faulting virtual address) and decode
     * the error code before the register dump. */
    if (frame->vector == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        uint64_t ec = frame->error_code;
        kprintf("  Fault addr : 0x%016lx\n", cr2);
        kprintf("  Reason     : %s | %s | %s%s\n",
                (ec & 1)  ? "protection-violation" : "not-present",
                (ec & 2)  ? "write"                : "read",
                (ec & 4)  ? "user-mode"            : "kernel-mode",
                (ec & 16) ? " | instruction-fetch" : "");
        kprintf("------------------------------------------------------\n");
    }

    kprintf("  Error Code : 0x%016lx\n", frame->error_code);
    kprintf("  RIP        : 0x%016lx\n", frame->rip);
    kprintf("  CS         : 0x%04lx\n",  frame->cs);
    kprintf("  RFLAGS     : 0x%016lx\n", frame->rflags);
    kprintf("  RSP        : 0x%016lx\n", frame->rsp);
    kprintf("  SS         : 0x%04lx\n",  frame->ss);
    kprintf("------------------------------------------------------\n");
    kprintf("  RAX = 0x%016lx   RBX = 0x%016lx\n", frame->rax, frame->rbx);
    kprintf("  RCX = 0x%016lx   RDX = 0x%016lx\n", frame->rcx, frame->rdx);
    kprintf("  RSI = 0x%016lx   RDI = 0x%016lx\n", frame->rsi, frame->rdi);
    kprintf("  RBP = 0x%016lx   R8  = 0x%016lx\n", frame->rbp, frame->r8);
    kprintf("  R9  = 0x%016lx   R10 = 0x%016lx\n", frame->r9,  frame->r10);
    kprintf("  R11 = 0x%016lx   R12 = 0x%016lx\n", frame->r11, frame->r12);
    kprintf("  R13 = 0x%016lx   R14 = 0x%016lx\n", frame->r13, frame->r14);
    kprintf("  R15 = 0x%016lx\n", frame->r15);
    kprintf("======================================================\n");

    for (;;) __asm__ volatile ("cli; hlt");
}
