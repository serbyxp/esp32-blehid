#ifndef WS_ASCII_H
#define WS_ASCII_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "hid_keyboard.h"

#define WS_ASCII_REPORT_COUNT 4

bool ws_ascii_prepare_reports(uint8_t ascii, keyboard_state_t out[WS_ASCII_REPORT_COUNT], size_t *out_count);

#ifdef UNIT_TEST
size_t ws_ascii_build_sequence(const uint8_t *text, size_t len, keyboard_state_t *out, size_t capacity);
size_t ws_ascii_build_char(uint8_t ascii, keyboard_state_t *out, size_t capacity);
#endif

#endif // WS_ASCII_H
