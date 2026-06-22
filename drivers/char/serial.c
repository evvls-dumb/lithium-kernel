#include "serial.h"
#include "../../include/io.h"

#define COM1_BASE 0x3F8

/* UART register offsets (relative to COM1_BASE). */
#define UART_DATA       0   /* Transmit/Receive data (DLAB=0) */
#define UART_IER        1   /* Interrupt Enable (DLAB=0)      */
#define UART_BAUD_LO    0   /* Baud divisor low  (DLAB=1)     */
#define UART_BAUD_HI    1   /* Baud divisor high (DLAB=1)     */
#define UART_IIR_FCR    2   /* FIFO control                   */
#define UART_LCR        3   /* Line Control                   */
#define UART_MCR        4   /* Modem Control                  */
#define UART_LSR        5   /* Line Status                    */

#define LCR_DLAB        0x80
#define LCR_8N1         0x03
#define LSR_TX_EMPTY    0x20

void serial_init(void) {
    outb(COM1_BASE + UART_IER,     0x00); /* Disable interrupts             */
    outb(COM1_BASE + UART_LCR,     LCR_DLAB);
    outb(COM1_BASE + UART_BAUD_LO, 0x03); /* Divisor = 3 → 38 400 baud     */
    outb(COM1_BASE + UART_BAUD_HI, 0x00);
    outb(COM1_BASE + UART_LCR,     LCR_8N1);
    outb(COM1_BASE + UART_IIR_FCR, 0xC7); /* Enable FIFO, clear, 14-byte   */
    outb(COM1_BASE + UART_MCR,     0x0B); /* RTS + DTR + IRQ enable         */
}

bool serial_ready(void) {
    return (inb(COM1_BASE + UART_LSR) & LSR_TX_EMPTY) != 0;
}

void serial_putchar(char c) {
    /* Expand \n to \r\n for terminal compatibility. */
    if (c == '\n') serial_putchar('\r');
    while (!serial_ready());
    outb(COM1_BASE + UART_DATA, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putchar(*s++);
}
