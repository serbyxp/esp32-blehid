#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "http_server.h"
#include "nvs.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wifi_mode_t s_stub_wifi_mode = WIFI_MODE_AP;
static int s_stub_scan_stop_calls = 0;
static int s_stub_scan_start_calls = 0;
static int s_stub_connect_calls = 0;
static bool s_stub_sta_started = false;
static bool s_stub_wifi_running = false;
static wifi_config_t s_stub_sta_config;
static TimerHandle_t s_last_timer = NULL;

static wifi_ap_record_t s_stub_ap_info;
static esp_netif_ip_info_t s_stub_ip_info = {0};

static esp_netif_t s_stub_sta_netif;
static esp_netif_t s_stub_ap_netif;

static TickType_t s_fake_tick = 0;

typedef void (*wifi_manager_delay_hook_t)(TickType_t current_tick);
static wifi_manager_delay_hook_t s_delay_hook = NULL;

TickType_t wifi_manager_test_get_tick_count(void)
{
    return s_fake_tick;
}

void wifi_manager_test_set_delay_hook(wifi_manager_delay_hook_t hook)
{
    s_delay_hook = hook;
}

TickType_t xTaskGetTickCount(void)
{
    return s_fake_tick;
}

void vTaskDelay(TickType_t xTicksToDelay)
{
    if (xTicksToDelay == 0)
    {
        xTicksToDelay = 1;
    }

    s_fake_tick += xTicksToDelay;

    if (s_delay_hook)
    {
        s_delay_hook(s_fake_tick);
    }
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
    s_stub_wifi_mode = mode;
    return ESP_OK;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode)
{
    if (!mode)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *mode = s_stub_wifi_mode;
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
    (void)interface;
    if (!conf)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_stub_sta_config, conf, sizeof(s_stub_sta_config));
    return ESP_OK;
}

esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf)
{
    (void)interface;
    if (!conf)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(conf, &s_stub_sta_config, sizeof(s_stub_sta_config));
    return ESP_OK;
}

esp_err_t esp_wifi_start(void)
{
    s_stub_wifi_running = true;
    s_stub_sta_started = true;
    return ESP_OK;
}

esp_err_t esp_wifi_stop(void)
{
    s_stub_wifi_running = false;
    s_stub_sta_started = false;
    return ESP_OK;
}

esp_err_t esp_wifi_connect(void)
{
    ++s_stub_connect_calls;
    return ESP_OK;
}

esp_err_t esp_wifi_disconnect(void)
{
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info)
{
    if (!ap_info)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(ap_info, &s_stub_ap_info, sizeof(*ap_info));
    return ESP_OK;
}

esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    (void)ifx;
    if (!mac)
    {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < 6; ++i)
    {
        mac[i] = (uint8_t)(0x11 + i);
    }
    return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
    (void)config;
    (void)block;
    ++s_stub_scan_start_calls;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_stop(void)
{
    ++s_stub_scan_stop_calls;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number)
{
    if (number)
    {
        *number = 0;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records)
{
    (void)number;
    (void)ap_records;
    return ESP_OK;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void)
{
    return &s_stub_sta_netif;
}

esp_netif_t *esp_netif_create_default_wifi_ap(void)
{
    return &s_stub_ap_netif;
}

esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *hostname)
{
    (void)netif;
    (void)hostname;
    return ESP_OK;
}

const char *esp_netif_get_ifkey(esp_netif_t *netif)
{
    (void)netif;
    return "stub_if";
}

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key)
{
    (void)if_key;
    return &s_stub_sta_netif;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip_info)
{
    (void)netif;
    if (!ip_info)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *ip_info = s_stub_ip_info;
    return ESP_OK;
}

esp_err_t esp_event_post(esp_event_base_t base, int32_t event_id, void *event_data, size_t event_data_size, uint32_t ticks_to_wait)
{
    (void)base;
    (void)event_id;
    (void)event_data;
    (void)event_data_size;
    (void)ticks_to_wait;
    return ESP_OK;
}

esp_err_t esp_event_loop_create_default(void)
{
    return ESP_OK;
}

esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id,
                                     void (*event_handler)(void *, esp_event_base_t, int32_t, void *),
                                     void *event_handler_arg)
{
    (void)event_base;
    (void)event_id;
    (void)event_handler;
    (void)event_handler_arg;
    return ESP_OK;
}

esp_err_t esp_netif_init(void)
{
    return ESP_OK;
}

TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod, int uxAutoReload, void *pvTimerID, TimerCallbackFunction_t pxCallbackFunction)
{
    (void)pcTimerName;
    (void)xTimerPeriod;
    (void)uxAutoReload;
    (void)pvTimerID;
    s_last_timer = malloc(1);
    (void)pxCallbackFunction;
    return s_last_timer;
}

int xTimerIsTimerActive(TimerHandle_t xTimer)
{
    return xTimer ? pdTRUE : pdFALSE;
}

int xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTimer;
    (void)xTicksToWait;
    return pdPASS;
}

int xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod, TickType_t xTicksToWait)
{
    (void)xTimer;
    (void)xNewPeriod;
    (void)xTicksToWait;
    return pdPASS;
}

int xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTimer;
    (void)xTicksToWait;
    return pdPASS;
}

esp_err_t http_server_enable_captive_portal(void)
{
    return ESP_OK;
}

esp_err_t http_server_disable_captive_portal(void)
{
    return ESP_OK;
}

void http_server_publish_wifi_status(void)
{
}

void http_server_publish_scan_results(const wifi_ap_record_t *records, uint16_t count)
{
    (void)records;
    (void)count;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    (void)name;
    (void)open_mode;
    if (out_handle)
    {
        *out_handle = (nvs_handle_t)0x1;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length)
{
    (void)handle;
    (void)key;
    if (length)
    {
        *length = 0;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t nvs_close(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t err)
{
    switch (err)
    {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_WIFI_NOT_INIT:
        return "ESP_ERR_WIFI_NOT_INIT";
    case ESP_ERR_WIFI_NOT_STARTED:
        return "ESP_ERR_WIFI_NOT_STARTED";
    case ESP_ERR_WIFI_STATE:
        return "ESP_ERR_WIFI_STATE";
    case ESP_ERR_WIFI_CONN:
        return "ESP_ERR_WIFI_CONN";
    case ESP_ERR_NVS_NOT_FOUND:
        return "ESP_ERR_NVS_NOT_FOUND";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

void wifi_manager_test_reset_stubs(void)
{
    s_stub_wifi_mode = WIFI_MODE_AP;
    s_stub_scan_stop_calls = 0;
    s_stub_scan_start_calls = 0;
    s_stub_connect_calls = 0;
    s_stub_sta_started = false;
    s_stub_wifi_running = false;
    memset(&s_stub_sta_config, 0, sizeof(s_stub_sta_config));
    memset(&s_stub_ap_info, 0, sizeof(s_stub_ap_info));
    memset(&s_stub_ip_info, 0, sizeof(s_stub_ip_info));
    s_fake_tick = 0;
    s_delay_hook = NULL;
}

int wifi_manager_test_get_scan_stop_calls(void)
{
    return s_stub_scan_stop_calls;
}

int wifi_manager_test_get_connect_calls(void)
{
    return s_stub_connect_calls;
}

wifi_mode_t wifi_manager_test_get_wifi_mode(void)
{
    return s_stub_wifi_mode;
}

wifi_config_t wifi_manager_test_get_sta_config(void)
{
    return s_stub_sta_config;
}

int wifi_manager_test_get_scan_start_calls(void)
{
    return s_stub_scan_start_calls;
}

bool wifi_manager_test_is_sta_started(void)
{
    return s_stub_sta_started;
}

