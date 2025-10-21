#include "wifi_manager.h"
#include "wifi_credentials.h"
#include "http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#ifdef __has_include
#if __has_include("mdns.h")
#define WIFI_MANAGER_HAS_MDNS 1
#include "mdns.h"
#else
#define WIFI_MANAGER_HAS_MDNS 0
#endif
#else
#define WIFI_MANAGER_HAS_MDNS 0
#endif
#include <string.h>

static const char *TAG = "WIFI_MGR";
static const char *NVS_WIFI_NAMESPACE = "wifi_config";

ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_EVENT);

enum
{
    WIFI_MANAGER_EVENT_RETRY_CONNECT = 0,
};

static bool s_wifi_connected = false;
static wifi_mode_t s_current_mode = WIFI_MODE_NULL;
static char s_hostname[32] = {0};
static char s_ap_ssid[32] = {0};
static int s_sta_retry_count = 0;
static bool s_scanning = false;
static wifi_scan_callback_t s_scan_callback = NULL;
static bool s_restore_ap_on_scan = false;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static TimerHandle_t s_sta_retry_timer = NULL;

#define MAX_STA_RETRY 5
#define STA_RETRY_DELAY_MS 5000
static void stop_wifi_if_running(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGW(TAG, "esp_wifi_stop returned %s", esp_err_to_name(err));
    }
}

static void sta_retry_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Retry timer expired, attempting to reconnect");

    if (s_scanning)
    {
        ESP_LOGW(TAG, "Retry skipped because STA is scanning");
        return;
    }

    esp_err_t err = esp_event_post(WIFI_MANAGER_EVENT, WIFI_MANAGER_EVENT_RETRY_CONNECT,
                                   NULL, 0, portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to post retry event: %s", esp_err_to_name(err));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        if (!s_scanning)
        {
            esp_wifi_connect();
            ESP_LOGI(TAG, "WiFi STA started, connecting...");
        }
        else
        {
            ESP_LOGI(TAG, "WiFi STA interface started for scanning");
        }
        http_server_publish_wifi_status();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_wifi_connected = false;

        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "WiFi disconnected, reason: %d", event->reason);

        // Check for auth failure
        if (event->reason == WIFI_REASON_AUTH_FAIL ||
            event->reason == WIFI_REASON_NO_AP_FOUND ||
            event->reason == WIFI_REASON_ASSOC_FAIL)
        {
            ESP_LOGW(TAG, "Connection failed (reason %d), will fallback to AP mode", event->reason);
            s_sta_retry_count = MAX_STA_RETRY; // Force fallback
            if (s_sta_retry_timer)
            {
                xTimerStop(s_sta_retry_timer, 0);
            }
        }
        else if (s_sta_retry_count < MAX_STA_RETRY)
        {
            s_sta_retry_count++;
            ESP_LOGI(TAG, "Retry %d/%d in %dms", s_sta_retry_count, MAX_STA_RETRY, STA_RETRY_DELAY_MS);
            if (s_sta_retry_timer)
            {
                if (xTimerIsTimerActive(s_sta_retry_timer) == pdTRUE)
                {
                    xTimerStop(s_sta_retry_timer, 0);
                }
                if (xTimerChangePeriod(s_sta_retry_timer, pdMS_TO_TICKS(STA_RETRY_DELAY_MS), 0) != pdPASS ||
                    xTimerStart(s_sta_retry_timer, 0) != pdPASS)
                {
                    ESP_LOGE(TAG, "Failed to schedule STA retry timer");
                }
            }
        }
        else
        {
            ESP_LOGW(TAG, "Max retries reached, fallback to AP mode");
            if (s_sta_retry_timer)
            {
                xTimerStop(s_sta_retry_timer, 0);
            }
        }
        http_server_publish_wifi_status();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        s_sta_retry_count = 0;
        if (s_sta_retry_timer)
        {
            xTimerStop(s_sta_retry_timer, 0);
        }

#if WIFI_MANAGER_HAS_MDNS
        // Update mDNS
        mdns_service_txt_item_set("_http", "_tcp", "ip", ip4addr_ntoa(&event->ip_info.ip));
#endif

        wifi_mode_t mode = WIFI_MODE_NULL;
        esp_err_t mode_err = esp_wifi_get_mode(&mode);
        if (mode_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to get WiFi mode after STA got IP: %s", esp_err_to_name(mode_err));
        }
        else if (mode == WIFI_MODE_APSTA)
        {
            ESP_LOGI(TAG, "STA connected while AP active; disabling AP interface");
            esp_err_t disable_err = wifi_manager_disable_ap();
            if (disable_err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to disable AP interface after STA connection: %s", esp_err_to_name(disable_err));
            }
        }
        http_server_publish_wifi_status();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined AP", MAC2STR(event->mac));
        http_server_publish_wifi_status();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left AP", MAC2STR(event->mac));
        http_server_publish_wifi_status();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG, "WiFi scan completed");
        s_scanning = false;

        if (s_scan_callback)
        {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);

            if (ap_count > 0)
            {
                wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
                if (ap_list)
                {
                    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
                    s_scan_callback(ap_list, ap_count);
                    free(ap_list);
                }
            }
            else
            {
                s_scan_callback(NULL, 0);
            }
            s_scan_callback = NULL;
        }
        http_server_publish_wifi_status();

        if (s_restore_ap_on_scan)
        {
            esp_wifi_set_mode(WIFI_MODE_AP);
            s_restore_ap_on_scan = false;
        }
    }
    else if (event_base == WIFI_MANAGER_EVENT && event_id == WIFI_MANAGER_EVENT_RETRY_CONNECT)
    {
        if (s_current_mode != WIFI_MODE_STA && s_current_mode != WIFI_MODE_APSTA)
        {
            ESP_LOGI(TAG, "Retry skipped because STA is not active");
            return;
        }

        if (s_scanning)
        {
            ESP_LOGW(TAG, "Retry skipped because STA is scanning");
            return;
        }

        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "esp_wifi_connect failed during retry: %s", esp_err_to_name(err));
        }
    }
}

static void generate_hostname_and_ssid(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Generate hostname: uHID-AABBCC
    snprintf(s_hostname, sizeof(s_hostname), "uHID-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    // Generate AP SSID: same as hostname
    strncpy(s_ap_ssid, s_hostname, sizeof(s_ap_ssid) - 1);

    ESP_LOGI(TAG, "Generated hostname/SSID: %s", s_hostname);
}

esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (!s_sta_retry_timer)
    {
        s_sta_retry_timer = xTimerCreate("sta_retry", pdMS_TO_TICKS(STA_RETRY_DELAY_MS), pdFALSE, NULL,
                                         sta_retry_timer_callback);
        if (!s_sta_retry_timer)
        {
            ESP_LOGE(TAG, "Failed to create STA retry timer");
            return ESP_FAIL;
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_MANAGER_EVENT, WIFI_MANAGER_EVENT_RETRY_CONNECT,
                                               &wifi_event_handler, NULL));

    generate_hostname_and_ssid();

#if WIFI_MANAGER_HAS_MDNS
    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(s_hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set(s_hostname));

    // Add mDNS services
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_ws", "_tcp", 8765, NULL, 0));
#else
    ESP_LOGW(TAG, "mDNS support not available (mdns component missing)");
#endif

    ESP_LOGI(TAG, "WiFi manager initialized, mDNS hostname: %s.local", s_hostname);
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(const char *ssid, const char *password)
{
    if (!s_ap_netif)
    {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    if (!s_ap_netif)
    {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (ssid && strlen(ssid) > 0)
    {
        strncpy(s_ap_ssid, ssid, sizeof(s_ap_ssid) - 1);
    }

    strncpy((char *)wifi_config.ap.ssid, s_ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(s_ap_ssid);

    if (password && strlen(password) > 0)
    {
        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    }
    else
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t mode_err = esp_wifi_get_mode(&mode);
    if (mode_err != ESP_OK && mode_err != ESP_ERR_WIFI_NOT_INIT)
    {
        ESP_LOGE(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(mode_err));
        return mode_err;
    }

    if (mode != WIFI_MODE_NULL && mode != WIFI_MODE_AP)
    {
        stop_wifi_if_running();
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN)
    {
        ESP_LOGE(TAG, "esp_wifi_start (AP) failed: %s", esp_err_to_name(start_err));
        return start_err;
    }

    s_current_mode = WIFI_MODE_AP;
    s_wifi_connected = false;
    s_sta_retry_count = 0;
    s_restore_ap_on_scan = false;

    if (s_sta_retry_timer)
    {
        xTimerStop(s_sta_retry_timer, 0);
    }

    ESP_LOGI(TAG, "AP started: SSID=%s, IP=192.168.4.1", s_ap_ssid);
    ESP_LOGI(TAG, "Access via: http://%s.local or http://192.168.4.1", s_hostname);

    http_server_publish_wifi_status();
    return ESP_OK;
}

esp_err_t wifi_manager_start_sta(const char *ssid, const char *password)
{
    if (!s_sta_netif)
    {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    if (!s_sta_netif)
    {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_FAIL;
    }

    // Set hostname for DHCP
    ESP_ERROR_CHECK(esp_netif_set_hostname(s_sta_netif, s_hostname));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password)
    {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t mode_err = esp_wifi_get_mode(&mode);
    if (mode_err != ESP_OK && mode_err != ESP_ERR_WIFI_NOT_INIT)
    {
        ESP_LOGE(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(mode_err));
        return mode_err;
    }

    bool ap_active = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    wifi_mode_t target_mode = ap_active ? WIFI_MODE_APSTA : WIFI_MODE_STA;

    if (mode != target_mode)
    {
        ESP_LOGI(TAG, "Switching WiFi mode from %d to %d", mode, target_mode);
    }

    // Ensure WiFi driver is in the desired mode before configuring the STA interface
    ESP_ERROR_CHECK(esp_wifi_set_mode(target_mode));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN)
    {
        ESP_LOGE(TAG, "esp_wifi_start (STA) failed: %s", esp_err_to_name(start_err));
        return start_err;
    }

    s_current_mode = target_mode;
    s_wifi_connected = false;
    s_sta_retry_count = 0;
    s_restore_ap_on_scan = false;

    ESP_LOGI(TAG, "STA started, connecting to: %s", ssid);
    http_server_publish_wifi_status();
    return ESP_OK;
}

esp_err_t wifi_manager_enable_apsta(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_WIFI_NOT_INIT)
        {
            ESP_LOGE(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    if (!s_sta_netif)
    {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (!s_sta_netif)
        {
            ESP_LOGE(TAG, "Failed to create STA netif for APSTA transition");
            return ESP_FAIL;
        }

        esp_err_t host_err = esp_netif_set_hostname(s_sta_netif, s_hostname);
        if (host_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to set STA hostname during APSTA transition: %s", esp_err_to_name(host_err));
        }
    }

    if (mode == WIFI_MODE_AP)
    {
        ESP_LOGI(TAG, "Enabling APSTA mode to keep AP active during STA connect");
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err == ESP_OK)
        {
            s_current_mode = WIFI_MODE_APSTA;
            http_server_publish_wifi_status();
        }
        else
        {
            ESP_LOGE(TAG, "Failed to enable APSTA mode: %s", esp_err_to_name(err));
        }
        return err;
    }

    if (mode != WIFI_MODE_APSTA)
    {
        ESP_LOGD(TAG, "APSTA enable requested while in mode %d", mode);
    }

    return ESP_OK;
}

esp_err_t wifi_manager_disable_ap(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_WIFI_NOT_INIT)
        {
            ESP_LOGE(TAG, "esp_wifi_get_mode failed while disabling AP: %s", esp_err_to_name(err));
        }
        return err;
    }

    if (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_AP)
    {
        ESP_LOGI(TAG, "Disabling AP interface after STA connection");
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK)
        {
            s_current_mode = WIFI_MODE_STA;
            http_server_publish_wifi_status();
        }
        else
        {
            ESP_LOGE(TAG, "Failed to disable AP interface: %s", esp_err_to_name(err));
        }
        return err;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_restore_ap_mode(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_WIFI_NOT_INIT)
        {
            ESP_LOGE(TAG, "esp_wifi_get_mode failed while restoring AP: %s", esp_err_to_name(err));
        }
        return err;
    }

    if (mode == WIFI_MODE_AP)
    {
        return ESP_OK;
    }

    if (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_STA)
    {
        ESP_LOGI(TAG, "Restoring AP-only mode after STA failure");
        esp_wifi_disconnect();
        err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err == ESP_OK)
        {
            s_current_mode = WIFI_MODE_AP;
            s_wifi_connected = false;
            s_sta_retry_count = 0;
            if (s_sta_retry_timer)
            {
                xTimerStop(s_sta_retry_timer, 0);
            }
            http_server_publish_wifi_status();
        }
        else
        {
            ESP_LOGE(TAG, "Failed to restore AP-only mode: %s", esp_err_to_name(err));
        }
        return err;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_start_scan(wifi_scan_callback_t callback)
{
    if (s_scanning)
    {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_current_mode == WIFI_MODE_STA && !s_wifi_connected)
    {
        ESP_LOGW(TAG, "Cannot start scan while STA is connecting");
        return ESP_ERR_INVALID_STATE;
    }

    s_scan_callback = callback;
    s_scanning = true;

    // If in AP mode, need to temporarily start STA for scanning
    s_restore_ap_on_scan = false;

    if (s_current_mode == WIFI_MODE_AP)
    {
        ESP_LOGI(TAG, "Switching to APSTA mode for scanning");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        s_restore_ap_on_scan = true;

        if (!s_sta_netif)
        {
            s_sta_netif = esp_netif_create_default_wifi_sta();
            if (!s_sta_netif)
            {
                ESP_LOGE(TAG, "Failed to ensure STA netif for scanning");
                s_scanning = false;
                s_scan_callback = NULL;
                return ESP_FAIL;
            }
            ESP_ERROR_CHECK(esp_netif_set_hostname(s_sta_netif, s_hostname));
        }

        esp_wifi_disconnect();
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, false);

    if (err != ESP_OK)
    {
        s_scanning = false;
        s_scan_callback = NULL;
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));

        // Restore AP-only mode if needed
        if (s_restore_ap_on_scan)
        {
            esp_wifi_set_mode(WIFI_MODE_AP);
            s_restore_ap_on_scan = false;
        }
        http_server_publish_wifi_status();
    }
    else
    {
        ESP_LOGI(TAG, "WiFi scan started");
        http_server_publish_wifi_status();
    }

    return err;
}

esp_err_t wifi_manager_stop_scan(void)
{
    if (s_scanning)
    {
        esp_wifi_scan_stop();
        s_scanning = false;
        s_scan_callback = NULL;

        // Restore AP-only mode if needed
        if (s_restore_ap_on_scan)
        {
            esp_wifi_set_mode(WIFI_MODE_AP);
            s_restore_ap_on_scan = false;
        }
        http_server_publish_wifi_status();
    }
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    wifi_manager_stop_scan();
    s_restore_ap_on_scan = false;

    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_NOT_STARTED)
    {
        err = ESP_OK;
    }

    if (err == ESP_OK)
    {
        s_current_mode = WIFI_MODE_NULL;
        s_wifi_connected = false;
        s_sta_retry_count = 0;
        if (s_sta_retry_timer)
        {
            xTimerStop(s_sta_retry_timer, 0);
        }
        ESP_LOGI(TAG, "WiFi stopped");
        http_server_publish_wifi_status();
    }
    else
    {
        ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
    }

    return err;
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}

bool wifi_manager_is_scanning(void)
{
    return s_scanning;
}

wifi_mode_t wifi_manager_get_mode(void)
{
    return s_current_mode;
}

int wifi_manager_get_retry_count(void)
{
    return s_sta_retry_count;
}

const char *wifi_manager_get_hostname(void)
{
    return s_hostname;
}

const char *wifi_manager_get_ap_ssid(void)
{
    return s_ap_ssid;
}

esp_err_t wifi_manager_get_ip(char *ip_str, size_t len)
{
    if (!ip_str || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = NULL;

    if (s_current_mode == WIFI_MODE_STA && s_wifi_connected)
    {
        netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    }
    else if (s_current_mode == WIFI_MODE_AP || s_current_mode == WIFI_MODE_APSTA)
    {
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }

    if (!netif)
    {
        return ESP_ERR_NOT_FOUND;
    }

    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip_info));

    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t wifi_manager_get_sta_info(wifi_manager_sta_info_t *info)
{
    if (!info)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(wifi_manager_sta_info_t));
    info->connected = s_wifi_connected;
    info->retry_count = s_sta_retry_count;

    if (s_current_mode == WIFI_MODE_STA || s_current_mode == WIFI_MODE_APSTA)
    {
        wifi_config_t wifi_config;
        esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
        strncpy(info->ssid, (char *)wifi_config.sta.ssid, sizeof(info->ssid) - 1);

        if (s_wifi_connected)
        {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
            {
                info->rssi = ap_info.rssi;
                memcpy(info->bssid, ap_info.bssid, 6);
            }

            wifi_manager_get_ip(info->ip, sizeof(info->ip));
        }
    }

    return ESP_OK;
}

esp_err_t wifi_manager_get_mac_str(wifi_interface_t iface, char *mac_str, size_t len)
{
    if (!mac_str || len < 18)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(iface, mac);
    if (err != ESP_OK)
    {
        return err;
    }

    snprintf(mac_str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t wifi_manager_save_config(const char *ssid, const char *password)
{
    if (!ssid)
    {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_credentials_error_t cred_err = wifi_credentials_validate(ssid, password);
    if (cred_err != WIFI_CREDENTIALS_OK)
    {
        ESP_LOGW(TAG,
                 "Rejecting WiFi config: ssid_len=%zu psk_len=%zu reason=%s",
                 wifi_credentials_ssid_length(ssid),
                 wifi_credentials_psk_length(password),
                 wifi_credentials_error_to_string(cred_err));
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK && password)
    {
        err = nvs_set_str(handle, "password", password);
    }

    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi config saved: %s", ssid);
    }

    return err;
}

esp_err_t wifi_manager_load_config(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t required_size = ssid_len;
    err = nvs_get_str(handle, "ssid", ssid, &required_size);

    if (err == ESP_OK && password)
    {
        required_size = pass_len;
        err = nvs_get_str(handle, "password", password, &required_size);
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            password[0] = '\0';
            err = ESP_OK;
        }
    }

    nvs_close(handle);
    return err;
}

esp_err_t wifi_manager_clear_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi config cleared");
    http_server_publish_wifi_status();
    return err;
}

bool wifi_manager_has_stored_config(void)
{
    char ssid[33] = {0};
    return wifi_manager_load_config(ssid, sizeof(ssid), NULL, 0) == ESP_OK && strlen(ssid) > 0;
}
