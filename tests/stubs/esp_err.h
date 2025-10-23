#ifndef ESP_ERR_H
#define ESP_ERR_H

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_WIFI_NOT_INIT 0x200
#define ESP_ERR_WIFI_NOT_STARTED 0x201
#define ESP_ERR_WIFI_NOT_STOPPED 0x202
#define ESP_ERR_WIFI_STATE 0x203
#define ESP_ERR_WIFI_CONN 0x204
#define ESP_ERR_NVS_NOT_FOUND 0x300
#define ESP_ERR_NOT_FOUND 0x301

const char *esp_err_to_name(esp_err_t err);

#define ESP_ERROR_CHECK(x)                           \
    do                                               \
    {                                                \
        esp_err_t __err_rc = (x);                    \
        if (__err_rc != ESP_OK)                      \
        {                                            \
            return __err_rc;                         \
        }                                            \
    } while (0)

#endif // ESP_ERR_H
