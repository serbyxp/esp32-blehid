#include "mouse_report_builder.h"

#include <string.h>

void mouse_build_report(const mouse_state_t *state, uint8_t report[HID_MOUSE_REPORT_LEN])
{
    if (!report)
    {
        return;
    }

    if (!state)
    {
        memset(report, 0, HID_MOUSE_REPORT_LEN);
        report[0] = HID_MOUSE_REPORT_ID;
        return;
    }

    report[0] = HID_MOUSE_REPORT_ID;
    report[1] = (uint8_t)(state->buttons & HID_MOUSE_BUTTON_MASK);
    report[2] = (uint8_t)state->x;
    report[3] = (uint8_t)state->y;
    report[4] = (uint8_t)state->wheel;
    report[5] = (uint8_t)state->hwheel;
}
