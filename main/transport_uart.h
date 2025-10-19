#ifndef TRANSPORT_UART_H
#define TRANSPORT_UART_H

#include "esp_err.h"
#include "hid_device.h"
#include "cJSON.h"

typedef struct
{
    void (*on_mouse)(const mouse_state_t *state);
    void (*on_keyboard)(const keyboard_state_t *state);
    void (*on_consumer)(const consumer_state_t *state);
    void (*on_control)(cJSON *message);
} transport_callbacks_t;

esp_err_t transport_uart_init(const transport_callbacks_t *callbacks);
esp_err_t transport_uart_deinit(void);
esp_err_t transport_uart_send(const char *message);

#endif // TRANSPORT_UART_H
