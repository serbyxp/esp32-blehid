#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdint.h>

#define DEFAULT_HTTP_PORT 80

esp_err_t http_server_start(uint16_t port);
esp_err_t http_server_stop(void);
void http_server_publish_wifi_status(void);
void http_server_publish_scan_results(const wifi_ap_record_t *records, uint16_t count);

#endif // HTTP_SERVER_H
