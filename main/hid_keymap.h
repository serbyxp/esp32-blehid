#ifndef HID_KEYMAP_H
#define HID_KEYMAP_H

#include <stdbool.h>
#include <stdint.h>
#include "hid_keyboard.h"

#define HID_KEYMAP_SHIFT 0x80
#define HID_KEYMAP_LEFT_SHIFT 0x02

bool hid_keymap_from_ascii(uint8_t ascii, uint8_t *keycode, uint8_t *modifiers);
bool hid_keymap_fill_state_from_ascii(uint8_t ascii, keyboard_state_t *state);

#endif // HID_KEYMAP_H
