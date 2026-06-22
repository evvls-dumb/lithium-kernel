#include "vga.h"

static uint16_t *const VGA_BUF = (uint16_t *)0xB8000;

static int     vga_row   = 0;
static int     vga_col   = 0;
static uint8_t vga_color = (VGA_BLACK << 4) | VGA_LIGHT_GREY;  /* grey on black */

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (uint8_t)((bg & 0xF) << 4) | (fg & 0xF);
}

void vga_clear(void) {
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_BUF[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    vga_row = 0;
    vga_col = 0;
}

void vga_init(void) {
    vga_clear();
}

static void vga_scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_BUF[y * VGA_WIDTH + x] = VGA_BUF[(y + 1) * VGA_WIDTH + x];
    for (int x = 0; x < VGA_WIDTH; x++)
        VGA_BUF[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    vga_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        if (++vga_row >= VGA_HEIGHT) vga_scroll();
        return;
    }
    if (c == '\r') {
        vga_col = 0;
        return;
    }
    if (c == '\t') {
        /* Advance to next 8-column tab stop. */
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            if (++vga_row >= VGA_HEIGHT) vga_scroll();
        }
        return;
    }
    VGA_BUF[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
    if (++vga_col >= VGA_WIDTH) {
        vga_col = 0;
        if (++vga_row >= VGA_HEIGHT) vga_scroll();
    }
}

void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
}
