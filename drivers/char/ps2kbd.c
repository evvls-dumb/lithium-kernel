#include "ps2kbd.h"
#include "../../kernel/irq.h"
#include "../../include/io.h"
#include "../../arch/x86_64/cpu/idt.h"

#define KBD_DATA      0x60
#define KBD_STATUS    0x64
#define KBD_CMD       0x64

#define KBD_STAT_OUT  0x01
#define KBD_STAT_IN   0x02
#define KBD_STAT_AUX  0x20

#define KBD_CMD_READ_CONFIG   0x20
#define KBD_CMD_WRITE_CONFIG  0x60
#define KBD_CMD_ENABLE_PORT1  0xAE

#define KBD_CONFIG_IRQ1       0x01
#define KBD_CONFIG_DISABLE1   0x10
#define KBD_CONFIG_TRANSLATE  0x40

#define KBD_BUF_SIZE 256

static volatile char     kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

volatile bool kbd_ctrl_held = false;
volatile bool kbd_alt_held  = false;

static bool kbd_shift;
static bool kbd_caps;
static bool kbd_set2_release;
static bool kbd_extended;
static uint8_t kbd_scancode_set;

static const char sc1_normal[128] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',
    0,   '*', 0,  ' ',
};

static const char sc1_shifted[128] = {
    0,   0,  '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',
    0,   '*', 0,  ' ',
};

static const char sc2_normal[128] = {
    [0x0D] = '\t',
    [0x0E] = '`',
    [0x15] = 'q', [0x16] = '1',
    [0x1A] = 'z', [0x1B] = 's', [0x1C] = 'a', [0x1D] = 'w',
    [0x1E] = '2',
    [0x21] = 'c', [0x22] = 'x', [0x23] = 'd', [0x24] = 'e',
    [0x25] = '4', [0x26] = '3',
    [0x29] = ' ',
    [0x2A] = 'v', [0x2B] = 'f', [0x2C] = 't', [0x2D] = 'r',
    [0x2E] = '5',
    [0x31] = 'n', [0x32] = 'b', [0x33] = 'h', [0x34] = 'g',
    [0x35] = 'y', [0x36] = '6',
    [0x3A] = 'm', [0x3B] = 'j', [0x3C] = 'u', [0x3D] = '7',
    [0x3E] = '8',
    [0x41] = ',', [0x42] = 'k', [0x43] = 'i', [0x44] = 'o',
    [0x45] = '0', [0x46] = '9',
    [0x49] = '.', [0x4A] = '/', [0x4B] = 'l', [0x4C] = ';',
    [0x4D] = 'p', [0x4E] = '-',
    [0x52] = '\'', [0x54] = '[', [0x55] = '=',
    [0x5A] = '\n', [0x5B] = ']', [0x5D] = '\\',
    [0x66] = '\b',
};

static const char sc2_shifted[128] = {
    [0x0D] = '\t',
    [0x0E] = '~',
    [0x15] = 'Q', [0x16] = '!',
    [0x1A] = 'Z', [0x1B] = 'S', [0x1C] = 'A', [0x1D] = 'W',
    [0x1E] = '@',
    [0x21] = 'C', [0x22] = 'X', [0x23] = 'D', [0x24] = 'E',
    [0x25] = '$', [0x26] = '#',
    [0x29] = ' ',
    [0x2A] = 'V', [0x2B] = 'F', [0x2C] = 'T', [0x2D] = 'R',
    [0x2E] = '%',
    [0x31] = 'N', [0x32] = 'B', [0x33] = 'H', [0x34] = 'G',
    [0x35] = 'Y', [0x36] = '^',
    [0x3A] = 'M', [0x3B] = 'J', [0x3C] = 'U', [0x3D] = '&',
    [0x3E] = '*',
    [0x41] = '<', [0x42] = 'K', [0x43] = 'I', [0x44] = 'O',
    [0x45] = ')', [0x46] = '(',
    [0x49] = '>', [0x4A] = '?', [0x4B] = 'L', [0x4C] = ':',
    [0x4D] = 'P', [0x4E] = '_',
    [0x52] = '"', [0x54] = '{', [0x55] = '+',
    [0x5A] = '\n', [0x5B] = '}', [0x5D] = '|',
    [0x66] = '\b',
};

static inline bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool is_alnum(char c) {
    return is_alpha(c) || (c >= '0' && c <= '9');
}

static inline bool is_modifier_set1(uint8_t key) {
    return key == 0x2A || key == 0x36 || key == 0x3A ||
           key == 0x1D || key == 0x38;
}

static inline bool is_modifier_set2(uint8_t key) {
    return key == 0x12 || key == 0x59 || key == 0x58 ||
           key == 0x14 || key == 0x11;
}

static void update_modifier(uint8_t set, uint8_t key, bool release) {
    if (set == 1) {
        if (key == 0x2A || key == 0x36) { kbd_shift = !release; return; }
        if (key == 0x3A && !release)     { kbd_caps ^= true;    return; }
        if (key == 0x1D)                 { kbd_ctrl_held = !release; return; }
        if (key == 0x38)                 { kbd_alt_held  = !release; return; }
    } else {
        if (key == 0x12 || key == 0x59) { kbd_shift = !release; return; }
        if (key == 0x58 && !release)    { kbd_caps ^= true;    return; }
        if (key == 0x14)                { kbd_ctrl_held = !release; return; }
        if (key == 0x11)                { kbd_alt_held  = !release; return; }
    }
}

static char translate(uint8_t set, uint8_t key) {
    if (key >= 128)
        return 0;

    bool shifted = kbd_shift;
    if (set == 1) {
        char normal = sc1_normal[key];
        bool use_upper = is_alpha(normal) ? (kbd_shift ^ kbd_caps) : shifted;
        return use_upper ? sc1_shifted[key] : normal;
    }

    char normal = sc2_normal[key];
    bool use_upper = is_alpha(normal) ? (kbd_shift ^ kbd_caps) : shifted;
    return use_upper ? sc2_shifted[key] : normal;
}

static uint8_t infer_set(uint8_t key) {
    char c1 = sc1_normal[key];
    char c2 = sc2_normal[key];

    if (c2 && !c1) return 2;
    if (c1 && !c2) return 1;
    if (c1 && c2) {
        if ((c1 == '\n' || c1 == '\b' || c1 == '\t') && is_alpha(c2))
            return 2;
        if ((c1 == '\n' || c1 == '\b' || c1 == '\t') && !is_alpha(c2))
            return 1;
        if (!is_alnum(c1) && is_alnum(c2)) return 2;
        return 1;
    }
    if (is_modifier_set2(key) && !is_modifier_set1(key)) return 2;
    return 1;
}

void kbd_decode_reset(void) {
    kbd_shift = false;
    kbd_caps = false;
    kbd_ctrl_held = false;
    kbd_alt_held = false;
    kbd_set2_release = false;
    kbd_extended = false;
    kbd_scancode_set = 0;
}

void kbd_decode_set_scancode_set(uint8_t set) {
    if (set <= 2)
        kbd_scancode_set = set;
}

bool kbd_decode_scancode(uint8_t scancode, char *out) {
    if (out) *out = 0;

    if (scancode == 0xE0) {
        kbd_extended = true;
        return false;
    }
    if (scancode == 0xF0) {
        kbd_set2_release = true;
        kbd_scancode_set = 2;
        return false;
    }

    uint8_t set = kbd_scancode_set;
    bool release = false;
    uint8_t key = scancode;

    if (set == 2 || kbd_set2_release) {
        set = 2;
        release = kbd_set2_release;
        kbd_set2_release = false;
    } else if (set == 1) {
        release = (scancode & 0x80) != 0;
        key = scancode & 0x7F;
    } else if (scancode & 0x80) {
        set = 1;
        release = true;
        key = scancode & 0x7F;
    } else {
        set = infer_set(scancode);
    }

    if (kbd_extended) {
        kbd_extended = false;
        if (set == 2 && key == 0x14) {
            kbd_ctrl_held = !release;
        }
        return false;
    }

    update_modifier(set, key, release);
    if ((set == 1 && is_modifier_set1(key)) ||
        (set == 2 && is_modifier_set2(key)) || release) {
        return false;
    }

    if (kbd_ctrl_held && ((set == 1 && key == 0x2E) ||
                          (set == 2 && key == 0x21))) {
        if (out) *out = 3;
        return true;
    }

    char c = translate(set, key);
    if (c) {
        kbd_scancode_set = set;
        if (out) *out = c;
        return true;
    }
    return false;
}

static inline void buf_push(char c) {
    uint32_t next = (kbd_tail + 1) & (KBD_BUF_SIZE - 1);
    if (next != kbd_head) {
        kbd_buf[kbd_tail] = c;
        kbd_tail = next;
    }
}

static void handle_scancode(uint8_t scancode) {
    char c = 0;
    if (kbd_decode_scancode(scancode, &c) && c)
        buf_push(c);
}

static bool kbd_wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(KBD_STATUS) & KBD_STAT_IN) == 0)
            return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool kbd_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(KBD_STATUS) & KBD_STAT_OUT)
            return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static void kbd_send_cmd(uint8_t cmd) {
    if (kbd_wait_input_clear())
        outb(KBD_CMD, cmd);
}

static void kbd_send_data(uint8_t data) {
    if (kbd_wait_input_clear())
        outb(KBD_DATA, data);
}

static void kbd_drain(void) {
    for (int i = 0; i < 32 && (inb(KBD_STATUS) & KBD_STAT_OUT); i++)
        (void)inb(KBD_DATA);
}

void kbd_poll(void) {
    for (int i = 0; i < 16; i++) {
        uint8_t status = inb(KBD_STATUS);
        if ((status & KBD_STAT_OUT) == 0)
            break;

        uint8_t scancode = inb(KBD_DATA);
        if (status & KBD_STAT_AUX)
            continue;
        handle_scancode(scancode);
    }
}

static void kbd_irq(uint8_t irq __attribute__((unused)),
                    interrupt_frame_t *frame __attribute__((unused))) {
    kbd_poll();
}

void kbd_init(void) {
    kbd_decode_reset();
    kbd_drain();

    kbd_send_cmd(KBD_CMD_ENABLE_PORT1);
    kbd_send_cmd(KBD_CMD_READ_CONFIG);

    uint8_t config = KBD_CONFIG_IRQ1 | KBD_CONFIG_TRANSLATE;
    if (kbd_wait_output_full())
        config = inb(KBD_DATA);

    config |= KBD_CONFIG_IRQ1;
    config |= KBD_CONFIG_TRANSLATE;
    config &= (uint8_t)~KBD_CONFIG_DISABLE1;

    kbd_send_cmd(KBD_CMD_WRITE_CONFIG);
    kbd_send_data(config);
    kbd_drain();
    kbd_decode_set_scancode_set(0);

    irq_register(1, kbd_irq);
    irq_unmask(1);
}

bool kbd_available(void) {
    return kbd_head != kbd_tail;
}

char kbd_trygetchar(void) {
    kbd_poll();
    if (!kbd_available()) return 0;
    char c = kbd_buf[kbd_head];
    kbd_head = (kbd_head + 1) & (KBD_BUF_SIZE - 1);
    return c;
}

char kbd_getchar(void) {
    while (!kbd_available()) {
        kbd_poll();
        if (!kbd_available())
            __asm__ volatile ("hlt");
    }
    return kbd_trygetchar();
}
