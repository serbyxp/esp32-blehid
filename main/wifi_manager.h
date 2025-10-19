#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    bool connected;
    char ssid[33];
    char ip[16];
    int8_t rssi;
    uint8_t bssid[6];
    int retry_count;
} wifi_manager_sta_info_t;

typedef void (*wifi_scan_callback_t)(const wifi_ap_record_t *records, uint16_t count);

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start_ap(const char *ssid, const char *password);
esp_err_t wifi_manager_start_sta(const char *ssid, const char *password);
esp_err_t wifi_manager_start_scan(wifi_scan_callback_t callback);
esp_err_t wifi_manager_stop_scan(void);
esp_err_t wifi_manager_stop(void);

bool wifi_manager_is_connected(void);
bool wifi_manager_is_scanning(void);
wifi_mode_t wifi_manager_get_mode(void);
int wifi_manager_get_retry_count(void);
const char *wifi_manager_get_hostname(void);
const char *wifi_manager_get_ap_ssid(void);
esp_err_t wifi_manager_get_ip(char *ip_str, size_t len);
esp_err_t wifi_manager_get_sta_info(wifi_manager_sta_info_t *info);
esp_err_t wifi_manager_get_mac_str(wifi_interface_t iface, char *mac_str, size_t len);

esp_err_t wifi_manager_save_config(const char *ssid, const char *password);
esp_err_t wifi_manager_load_config(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len);
esp_err_t wifi_manager_clear_config(void);
bool wifi_manager_has_stored_config(void);

#endif // WIFI_MANAGER_H
