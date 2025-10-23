#ifndef ESP_WIFI_TYPES_H
#define ESP_WIFI_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA = 1,
    WIFI_MODE_AP = 2,
    WIFI_MODE_APSTA = 3,
} wifi_mode_t;

typedef enum
{
    WIFI_IF_STA = 0,
    WIFI_IF_AP = 1,
} wifi_interface_t;

typedef enum
{
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA2_PSK = 4,
} wifi_auth_mode_t;

typedef enum
{
    WIFI_SCAN_TYPE_ACTIVE = 0,
    WIFI_SCAN_TYPE_PASSIVE = 1,
} wifi_scan_type_t;

typedef struct
{
    wifi_auth_mode_t authmode;
} wifi_connect_threshold_t;

typedef struct
{
    bool capable;
    bool required;
} wifi_pmf_config_t;

typedef struct
{
    uint8_t ssid[32];
    uint8_t password[64];
    wifi_connect_threshold_t threshold;
    wifi_pmf_config_t pmf_cfg;
    bool bssid_set;
} wifi_sta_config_t;

typedef struct
{
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t ssid_len;
    uint8_t channel;
    wifi_auth_mode_t authmode;
    wifi_pmf_config_t pmf_cfg;
    uint8_t max_connection;
    uint16_t beacon_interval;
} wifi_ap_config_t;

typedef struct
{
    wifi_sta_config_t sta;
    wifi_ap_config_t ap;
} wifi_config_t;

typedef struct
{
    int dummy;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})

typedef struct
{
    const uint8_t *ssid;
    const uint8_t *bssid;
    uint8_t channel;
    bool show_hidden;
    wifi_scan_type_t scan_type;
} wifi_scan_config_t;

typedef struct
{
    uint8_t bssid[6];
    uint8_t ssid[32];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;

typedef struct
{
    uint8_t mac[6];
    int aid;
} wifi_event_ap_staconnected_t;

typedef struct
{
    uint8_t mac[6];
    int aid;
} wifi_event_ap_stadisconnected_t;

typedef struct
{
    uint8_t reason;
} wifi_event_sta_disconnected_t;

#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(mac) (mac)[0], (mac)[1], (mac)[2], (mac)[3], (mac)[4], (mac)[5]

enum
{
    WIFI_EVENT_STA_START = 0,
    WIFI_EVENT_STA_STOP,
    WIFI_EVENT_STA_CONNECTED,
    WIFI_EVENT_STA_DISCONNECTED,
    WIFI_EVENT_AP_START,
    WIFI_EVENT_AP_STOP,
    WIFI_EVENT_AP_STACONNECTED,
    WIFI_EVENT_AP_STADISCONNECTED,
    WIFI_EVENT_SCAN_DONE,
};

#ifndef WIFI_REASON_AUTH_FAIL
#define WIFI_REASON_AUTH_FAIL 202
#endif
#ifndef WIFI_REASON_AUTH_EXPIRE
#define WIFI_REASON_AUTH_EXPIRE 203
#endif
#ifndef WIFI_REASON_HANDSHAKE_TIMEOUT
#define WIFI_REASON_HANDSHAKE_TIMEOUT 204
#endif
#ifndef WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 205
#endif
#ifndef WIFI_REASON_CONNECTION_FAIL
#define WIFI_REASON_CONNECTION_FAIL 206
#endif
#ifndef WIFI_REASON_NO_AP_FOUND
#define WIFI_REASON_NO_AP_FOUND 201
#endif

#endif // ESP_WIFI_TYPES_H
