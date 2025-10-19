#include "hid_keymap.h"

#include "hid_keymap.h"

#include <string.h>

static const uint8_t s_ascii_map[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 42, 43, 40, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    44, 158, 180, 160, 161, 162, 164, 52, 166, 167, 165, 174, 54, 45, 55, 56,
    39, 30, 31, 32, 33, 34, 35, 36, 37, 38, 179, 51, 182, 46, 183, 184,
    159, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
    147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 47, 49, 48, 163, 173,
    53, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 175, 177, 176, 181, 0};

static inline uint8_t sanitize_ascii(uint8_t ascii)
{
    if (ascii == '\r')
    {
        return '\n';
    }
    return ascii;
}

bool hid_keymap_from_ascii(uint8_t ascii, uint8_t *keycode, uint8_t *modifiers)
{
    ascii = sanitize_ascii(ascii);

    if (ascii >= 128)
    {
        return false;
    }

    uint8_t entry = s_ascii_map[ascii];
    if (entry == 0)
    {
        return false;
    }

    uint8_t mods = 0;
    if (entry & HID_KEYMAP_SHIFT)
    {
        mods |= HID_KEYMAP_LEFT_SHIFT;
        entry &= 0x7F;
    }

    if (entry == 0)
    {
        return false;
    }

    if (keycode)
    {
        *keycode = entry;
    }
    if (modifiers)
    {
        *modifiers = mods;
    }
    return true;
}

bool hid_keymap_fill_state_from_ascii(uint8_t ascii, keyboard_state_t *state)
{
    if (!state)
    {
        return false;
    }

    uint8_t keycode = 0;
    uint8_t mods = 0;
    if (!hid_keymap_from_ascii(ascii, &keycode, &mods))
    {
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->modifiers = mods;
    state->keys[0] = keycode;
    return true;
}
