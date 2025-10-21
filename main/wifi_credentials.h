#pragma once

#include <stddef.h>

typedef enum
{
    WIFI_CREDENTIALS_OK = 0,
    WIFI_CREDENTIALS_ERR_SSID_TOO_LONG,
    WIFI_CREDENTIALS_ERR_PSK_TOO_SHORT,
    WIFI_CREDENTIALS_ERR_PSK_TOO_LONG,
} wifi_credentials_error_t;

#define WIFI_CREDENTIALS_MAX_SSID_LEN 32
#define WIFI_CREDENTIALS_MIN_PSK_LEN 8
#define WIFI_CREDENTIALS_MAX_PSK_LEN 63

wifi_credentials_error_t wifi_credentials_validate(const char *ssid, const char *psk);
const char *wifi_credentials_error_to_string(wifi_credentials_error_t error);

size_t wifi_credentials_ssid_length(const char *ssid);
size_t wifi_credentials_psk_length(const char *psk);
