#include "ws_ascii.h"

#include <string.h>

#include "hid_keymap.h"

bool ws_ascii_prepare_reports(uint8_t ascii, keyboard_state_t out[WS_ASCII_REPORT_COUNT])
{
    if (!out)
    {
        return false;
    }

    memset(out, 0, sizeof(keyboard_state_t) * WS_ASCII_REPORT_COUNT);

    if (!hid_keymap_fill_state_from_ascii(ascii, &out[0]))
    {
        return false;
    }

    // Release report remains zeroed to ensure modifiers clear between characters.
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

        if (ws_ascii_prepare_reports(text[i], &out[produced]))
        {
            produced += WS_ASCII_REPORT_COUNT;
        }
    }

    return produced;
}
#endif
