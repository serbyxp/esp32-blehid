#include "wifi_credentials.h"

#include <string.h>

size_t wifi_credentials_ssid_length(const char *ssid)
{
    return ssid ? strlen(ssid) : 0;
}

size_t wifi_credentials_psk_length(const char *psk)
{
    if (!psk)
    {
        return 0;
    }
    return strlen(psk);
}

wifi_credentials_error_t wifi_credentials_validate(const char *ssid, const char *psk)
{
    size_t ssid_len = wifi_credentials_ssid_length(ssid);
    if (ssid_len > WIFI_CREDENTIALS_MAX_SSID_LEN)
    {
        return WIFI_CREDENTIALS_ERR_SSID_TOO_LONG;
    }

    size_t psk_len = wifi_credentials_psk_length(psk);
    if (psk_len == 0)
    {
        return WIFI_CREDENTIALS_OK;
    }

    if (psk_len < WIFI_CREDENTIALS_MIN_PSK_LEN)
    {
        return WIFI_CREDENTIALS_ERR_PSK_TOO_SHORT;
    }

    if (psk_len > WIFI_CREDENTIALS_MAX_PSK_LEN)
    {
        return WIFI_CREDENTIALS_ERR_PSK_TOO_LONG;
    }

    return WIFI_CREDENTIALS_OK;
}

const char *wifi_credentials_error_to_string(wifi_credentials_error_t error)
{
    switch (error)
    {
    case WIFI_CREDENTIALS_OK:
        return "ok";
    case WIFI_CREDENTIALS_ERR_SSID_TOO_LONG:
        return "ssid_too_long";
    case WIFI_CREDENTIALS_ERR_PSK_TOO_SHORT:
        return "psk_too_short";
    case WIFI_CREDENTIALS_ERR_PSK_TOO_LONG:
        return "psk_too_long";
    }

    return "unknown";
}
