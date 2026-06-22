#include "kprintf.h"
#include "../drivers/char/vga.h"
#include "../drivers/char/serial.h"

#include <stdarg.h>

void kputchar(char c) {
    vga_putchar(c);
    serial_putchar(c);
}

static void kputs(const char *s) {
    while (*s) kputchar(*s++);
}

/* Print an unsigned integer in the given base.
   Pads to `width` chars using `pad` on the left. */
static void print_uint(uint64_t n, int base, bool upper, int width, char pad) {
    static const char lo[] = "0123456789abcdef";
    static const char hi[] = "0123456789ABCDEF";
    const char *digits = upper ? hi : lo;

    char buf[64];
    int  len = 0;

    if (n == 0) {
        buf[len++] = '0';
    } else {
        while (n > 0) {
            buf[len++] = digits[n % (uint64_t)base];
            n /= (uint64_t)base;
        }
    }

    /* Left-pad to requested width. */
    for (int i = len; i < width; i++) kputchar(pad);

    /* Digits were accumulated in reverse — print right-to-left. */
    for (int i = len - 1; i >= 0; i--) kputchar(buf[i]);
}

static void print_int(int64_t n, int width, char pad) {
    if (n < 0) {
        kputchar('-');
        if (width > 0) width--;
        /* Use unsigned negation to handle INT64_MIN correctly. */
        print_uint((uint64_t)(-(n + 1)) + 1ULL, 10, false, width, pad);
    } else {
        print_uint((uint64_t)n, 10, false, width, pad);
    }
}

void kprintf(const char *fmt, ...) {
    /* kprintf uses global VGA + serial state — not re-entrant.
     * Disable interrupts for the duration so the scheduler cannot
     * preempt us and cause a second kprintf to corrupt the state.
     * Phase 13 will replace this with a proper spinlock. */
    uint64_t rflags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(rflags));

    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            kputchar(*fmt);
            continue;
        }

        fmt++; /* skip '%' */

        /* ── Parse optional flags / width ─────────────────── */
        char pad   = ' ';
        int  width = 0;
        bool lng   = false;  /* 'l' modifier */

        if (*fmt == '0') { pad = '0'; fmt++; }

        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        if (*fmt == 'l') { lng = true; fmt++; }

        /* ── Format specifier ──────────────────────────────── */
        switch (*fmt) {
        case 'd': case 'i': {
            int64_t v = lng ? va_arg(ap, long) : (int64_t)va_arg(ap, int);
            print_int(v, width, pad);
            break;
        }
        case 'u': {
            uint64_t v = lng ? va_arg(ap, unsigned long)
                             : (uint64_t)va_arg(ap, unsigned int);
            print_uint(v, 10, false, width, pad);
            break;
        }
        case 'x': {
            uint64_t v = lng ? va_arg(ap, unsigned long)
                             : (uint64_t)va_arg(ap, unsigned int);
            print_uint(v, 16, false, width, pad);
            break;
        }
        case 'X': {
            uint64_t v = lng ? va_arg(ap, unsigned long)
                             : (uint64_t)va_arg(ap, unsigned int);
            print_uint(v, 16, true, width, pad);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
            kputs("0x");
            print_uint(v, 16, false, 16, '0');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            kputs(s ? s : "(null)");
            break;
        }
        case 'c':
            kputchar((char)va_arg(ap, int));
            break;
        case '%':
            kputchar('%');
            break;
        default:
            kputchar('%');
            kputchar(*fmt);
            break;
        }
    }

    va_end(ap);

    __asm__ volatile ("push %0; popfq" : : "r"(rflags));
}
