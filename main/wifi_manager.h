#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    bool connected;
    bool connecting;
    char ssid[33];
    char ip[16];
    int8_t rssi;
    uint8_t bssid[6];
    int retry_count;
} wifi_manager_sta_info_t;

typedef void (*wifi_scan_callback_t)(const wifi_ap_record_t *records, uint16_t count);

typedef struct
{
    bool saved;
    bool connected;
    bool fallback_ap;
    bool status_changed;
    bool timed_out;
    wifi_mode_t final_mode;
    esp_err_t err;
    const char *error_key;
} wifi_manager_connect_result_t;

#define WIFI_MANAGER_DEFAULT_AP_PASS "composite"

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start_ap(const char *ssid, const char *password);
esp_err_t wifi_manager_start_sta(const char *ssid, const char *password);
esp_err_t wifi_manager_enable_apsta(void);
esp_err_t wifi_manager_disable_ap(void);
esp_err_t wifi_manager_restore_ap_mode(void);
esp_err_t wifi_manager_start_scan(wifi_scan_callback_t callback);
esp_err_t wifi_manager_stop_scan(void);
esp_err_t wifi_manager_stop(void);
esp_err_t wifi_manager_ensure_ap_only(const char *ssid, const char *password);
esp_err_t wifi_manager_connect_with_fallback(const char *ssid, const char *password,
                                             TickType_t timeout_ticks, wifi_manager_connect_result_t *result);
esp_err_t wifi_manager_save_and_connect(const char *ssid, const char *password,
                                        TickType_t timeout_ticks, wifi_manager_connect_result_t *result);

bool wifi_manager_is_connected(void);
bool wifi_manager_is_connecting(void);
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

#ifdef UNIT_TEST
void wifi_manager_test_reset_state(void);
void wifi_manager_test_set_state(bool scanning, bool restore_ap_on_scan, wifi_mode_t mode);
bool wifi_manager_test_get_deferred_ap_restore(void);
void wifi_manager_test_invoke_event(esp_event_base_t base, int32_t event_id, void *event_data);
void wifi_manager_test_invoke_wifi_event(int32_t event_id, void *event_data);
void wifi_manager_test_invoke_ip_event(int32_t event_id, void *event_data);
#endif

#endif // WIFI_MANAGER_H
