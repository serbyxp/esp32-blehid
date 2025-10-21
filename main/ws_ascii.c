#include "ws_ascii.h"

#include <string.h>

#include "hid_keymap.h"

bool ws_ascii_prepare_reports(uint8_t ascii, keyboard_state_t out[WS_ASCII_REPORT_COUNT], size_t *out_count)
{
    if (!out || !out_count)
    {
        return false;
    }

    *out_count = 0;
    memset(out, 0, sizeof(keyboard_state_t) * WS_ASCII_REPORT_COUNT);

    keyboard_state_t pressed = {0};
    if (!hid_keymap_fill_state_from_ascii(ascii, &pressed))
    {
        return false;
    }

    size_t produced = 0;
    bool has_modifier = pressed.modifiers != 0;

    if (has_modifier)
    {
        out[produced++] = (keyboard_state_t){
            .modifiers = pressed.modifiers,
        };
    }

    out[produced++] = pressed;

    if (has_modifier)
    {
        out[produced++] = (keyboard_state_t){
            .modifiers = pressed.modifiers,
        };
    }

    out[produced++] = (keyboard_state_t){0};

    *out_count = produced;
    return true;
}

#ifdef UNIT_TEST
size_t ws_ascii_build_sequence(const uint8_t *text, size_t len, keyboard_state_t *out, size_t capacity)
{
    if (!text || !out)
    {
        return 0;
    }

    size_t produced = 0;
    for (size_t i = 0; i < len; ++i)
    {
        if (capacity < produced + WS_ASCII_REPORT_COUNT)
        {
            break;
        }

        size_t char_count = 0;
        if (ws_ascii_prepare_reports(text[i], &out[produced], &char_count) && char_count > 0)
        {
            produced += char_count;
        }
    }

    return produced;
}

size_t ws_ascii_build_char(uint8_t ascii, keyboard_state_t *out, size_t capacity)
{
    if (!out || capacity < WS_ASCII_REPORT_COUNT)
    {
        return 0;
    }

    size_t count = 0;
    if (!ws_ascii_prepare_reports(ascii, out, &count))
    {
        return 0;
    }

    return count;
}
#endif
