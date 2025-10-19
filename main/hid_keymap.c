#include "hid_keymap.h"
#include "hid_ascii_table.h"
#include <string.h>

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

    uint8_t entry = hid_ascii_table[ascii];
    if (entry == 0)
    {
        return false;
    }

    uint8_t mods = 0;
    uint8_t key = entry;

    // FIXED: Properly extract shift bit and keycode
    if (entry & HID_KEYMAP_SHIFT)
    {
        mods |= HID_KEYMAP_LEFT_SHIFT;
        key = entry & 0x7F; // Mask off the shift bit
    }

    // Verify we have a valid keycode after masking
    if (key == 0)
    {
        return false;
    }

    if (keycode)
    {
        *keycode = key;
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