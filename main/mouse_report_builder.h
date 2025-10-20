#ifndef MOUSE_REPORT_BUILDER_H
#define MOUSE_REPORT_BUILDER_H

#include <stdint.h>
#include "hid_device.h"

#define HID_MOUSE_REPORT_ID 0x01
#define HID_MOUSE_BUTTON_MASK 0x1F
#define HID_MOUSE_REPORT_LEN 6

void mouse_build_report(const mouse_state_t *state, uint8_t report[HID_MOUSE_REPORT_LEN]);

#endif // MOUSE_REPORT_BUILDER_H
