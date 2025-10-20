#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include <stdint.h>

// Keyboard state used for HID reports
typedef struct
{
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} keyboard_state_t;

#endif // HID_KEYBOARD_H
