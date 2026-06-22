#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../drivers/char/ps2kbd.h"

static void feed(const uint8_t *scancodes, int count, char *out) {
    int pos = 0;
    for (int i = 0; i < count; i++) {
        char c = 0;
        if (kbd_decode_scancode(scancodes[i], &c) && c)
            out[pos++] = c;
    }
    out[pos] = '\0';
}

int main(void) {
    char out[16];

    const uint8_t set1_help[] = { 0x23, 0x12, 0x26, 0x19, 0x1C };
    feed(set1_help, (int)(sizeof(set1_help) / sizeof(set1_help[0])), out);
    assert(strcmp(out, "help\n") == 0);

    kbd_decode_reset();
    kbd_decode_set_scancode_set(1);
    const uint8_t set1_help_with_releases[] = {
        0x23, 0xA3, 0x12, 0x92, 0x26, 0xA6, 0x19, 0x99, 0x1C, 0x9C
    };
    feed(set1_help_with_releases,
         (int)(sizeof(set1_help_with_releases) / sizeof(set1_help_with_releases[0])),
         out);
    assert(strcmp(out, "help\n") == 0);

    kbd_decode_reset();
    kbd_decode_set_scancode_set(1);
    const uint8_t set1_backspace[] = { 0x1E, 0x9E, 0x0E, 0x8E };
    feed(set1_backspace,
         (int)(sizeof(set1_backspace) / sizeof(set1_backspace[0])),
         out);
    assert(strcmp(out, "a\b") == 0);

    kbd_decode_reset();
    const uint8_t set2_help[] = { 0x33, 0x24, 0x4B, 0x4D, 0x5A };
    feed(set2_help, (int)(sizeof(set2_help) / sizeof(set2_help[0])), out);
    assert(strcmp(out, "help\n") == 0);

    kbd_decode_reset();
    const uint8_t set2_help_with_releases[] = {
        0x33, 0xF0, 0x33, 0x24, 0xF0, 0x24,
        0x4B, 0xF0, 0x4B, 0x4D, 0xF0, 0x4D,
        0x5A, 0xF0, 0x5A
    };
    feed(set2_help_with_releases,
         (int)(sizeof(set2_help_with_releases) / sizeof(set2_help_with_releases[0])),
         out);
    assert(strcmp(out, "help\n") == 0);

    kbd_decode_reset();
    const uint8_t set2_a_backspace[] = { 0x1C, 0x66 };
    feed(set2_a_backspace,
         (int)(sizeof(set2_a_backspace) / sizeof(set2_a_backspace[0])),
         out);
    assert(strcmp(out, "a\b") == 0);

    kbd_decode_reset();
    kbd_decode_set_scancode_set(2);
    const uint8_t translated_set1_release_under_set2[] = { 0x23, 0xA3 };
    feed(translated_set1_release_under_set2,
         (int)(sizeof(translated_set1_release_under_set2) /
               sizeof(translated_set1_release_under_set2[0])),
         out);
    assert(strcmp(out, "d") == 0);

    kbd_decode_reset();
    kbd_decode_set_scancode_set(2);
    const uint8_t set2_shift_a[] = { 0x12, 0x1C, 0xF0, 0x1C, 0xF0, 0x12 };
    feed(set2_shift_a, (int)(sizeof(set2_shift_a) / sizeof(set2_shift_a[0])), out);
    assert(strcmp(out, "A") == 0);

    return 0;
}
