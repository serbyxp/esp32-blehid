#ifndef TRANSPORT_WEBSOCKET_H
#define TRANSPORT_WEBSOCKET_H

#include "esp_err.h"
#include "transport_uart.h"

#define DEFAULT_WS_PORT 8765

esp_err_t transport_websocket_init(const transport_callbacks_t *callbacks, uint16_t port);
esp_err_t transport_websocket_deinit(void);
esp_err_t transport_websocket_send(const char *message);

#endif // TRANSPORT_WEBSOCKET_H
