#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "hid_device.h"
#include "ble_hid.h"
#include "transport_uart.h"
#include "transport_ws.h"
#include "wifi_manager.h"
#include "http_server.h"

static const char *TAG = "MAIN";

#define DEFAULT_AP_PASS "composite"
#define NOTIFY_INTERVAL_MS 50

static hid_device_t *g_device = NULL;
static TimerHandle_t g_notify_timer = NULL;
static bool g_advertising_enabled = true;

// Remote state from transports
static struct
{
    mouse_state_t mouse;
    keyboard_state_t keyboard;
    consumer_state_t consumer;
    bool mouse_hold;
    bool wheel_hold;
} g_remote_state = {0};

static void populate_wifi_status_json(cJSON *obj)
{
    if (!obj)
    {
        return;
    }

    wifi_mode_t mode = wifi_manager_get_mode();
    const char *mode_str = "none";

    if (mode == WIFI_MODE_STA)
    {
        mode_str = wifi_manager_is_connected() ? "sta" : "connecting";
    }
    else if (mode == WIFI_MODE_AP)
    {
        mode_str = "ap";
    }
    else if (mode == WIFI_MODE_APSTA)
    {
        mode_str = wifi_manager_is_connected() ? "apsta" : "ap";
    }

    cJSON_AddStringToObject(obj, "mode", mode_str);
    cJSON_AddBoolToObject(obj, "connected", wifi_manager_is_connected());
    cJSON_AddBoolToObject(obj, "scanning", wifi_manager_is_scanning());
    cJSON_AddNumberToObject(obj, "retry_count", wifi_manager_get_retry_count());
    cJSON_AddStringToObject(obj, "hostname", wifi_manager_get_hostname());
    cJSON_AddStringToObject(obj, "ap_ssid", wifi_manager_get_ap_ssid());

    char ip[16] = {0};
    if (wifi_manager_get_ip(ip, sizeof(ip)) == ESP_OK)
    {
        cJSON_AddStringToObject(obj, "ip", ip);
    }

    wifi_manager_sta_info_t sta_info;
    if (wifi_manager_get_sta_info(&sta_info) == ESP_OK)
    {
        cJSON *sta = cJSON_CreateObject();
        if (sta)
        {
            cJSON_AddBoolToObject(sta, "connected", sta_info.connected);
            cJSON_AddStringToObject(sta, "ssid", sta_info.ssid);
            cJSON_AddStringToObject(sta, "ip", sta_info.ip);
            cJSON_AddNumberToObject(sta, "rssi", sta_info.rssi);
            cJSON_AddNumberToObject(sta, "retry_count", sta_info.retry_count);
            cJSON_AddItemToObject(obj, "sta", sta);
        }
    }
}

static void notify_wifi_status(void)
{
    http_server_publish_wifi_status();

    cJSON *msg = cJSON_CreateObject();
    if (!msg)
    {
        return;
    }

    cJSON_AddStringToObject(msg, "type", "wifi_status");
    populate_wifi_status_json(msg);

    char *payload = cJSON_PrintUnformatted(msg);
    if (payload)
    {
        transport_uart_send(payload);
        free(payload);
    }

    cJSON_Delete(msg);
}

static void broadcast_ble_status(void)
{
    ble_connection_info_t info = {0};
    ble_hid_get_connection_info(&info);

    cJSON *json = cJSON_CreateObject();
    if (!json)
    {
        return;
    }

    cJSON_AddStringToObject(json, "type", "ble_status");
    cJSON_AddBoolToObject(json, "connected", info.connected);
    cJSON_AddBoolToObject(json, "bonded", info.bonded);
    cJSON_AddBoolToObject(json, "encrypted", info.encrypted);
    cJSON_AddBoolToObject(json, "authenticated", info.authenticated);
    cJSON_AddNumberToObject(json, "addr_type", info.peer_addr_type);

    char addr_str[18] = "-";
    bool has_addr = false;
    for (int i = 0; i < 6; ++i)
    {
        if (info.peer_addr[i] != 0)
        {
            has_addr = true;
            break;
        }
    }
    if (has_addr)
    {
        snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 info.peer_addr[0], info.peer_addr[1], info.peer_addr[2],
                 info.peer_addr[3], info.peer_addr[4], info.peer_addr[5]);
    }
    cJSON_AddStringToObject(json, "peer_addr", addr_str);

    char *payload = cJSON_PrintUnformatted(json);
    if (payload)
    {
        transport_uart_send(payload);
        transport_websocket_send(payload);
        free(payload);
    }
    cJSON_Delete(json);
}

static void device_state_changed(hid_device_state_t state)
{
    const char *state_str = "UNKNOWN";
    switch (state)
    {
    case DEVICE_STATE_STOPPED:
        state_str = "STOPPED";
        break;
    case DEVICE_STATE_IDLE:
        state_str = "IDLE";
        break;
    case DEVICE_STATE_ADVERTISING:
        state_str = "ADVERTISING";
        break;
    case DEVICE_STATE_CONNECTED:
        state_str = "CONNECTED";
        break;
    }
    ESP_LOGI(TAG, "Device state changed: %s", state_str);

    if (state == DEVICE_STATE_IDLE && g_advertising_enabled)
    {
        esp_err_t err = hid_device_start_advertising(g_device);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to start advertising: %s", esp_err_to_name(err));
        }
    }

    // Send state update to transports
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "state");
    cJSON_AddStringToObject(json, "state", state_str);
    cJSON_AddBoolToObject(json, "bonded", hid_device_is_bonded(g_device));

    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str)
    {
        transport_uart_send(json_str);
        transport_websocket_send(json_str);
        free(json_str);
    }
    cJSON_Delete(json);

    broadcast_ble_status();
}

static void on_mouse_input(const mouse_state_t *state)
{
    if (!state)
        return;

    bool changed = false;

    if (state->x != g_remote_state.mouse.x || state->y != g_remote_state.mouse.y)
    {
        g_remote_state.mouse.x = state->x;
        g_remote_state.mouse.y = state->y;
        changed = true;
    }

    if (state->wheel != g_remote_state.mouse.wheel)
    {
        g_remote_state.mouse.wheel = state->wheel;
        changed = true;
    }

    if (state->buttons != g_remote_state.mouse.buttons)
    {
        g_remote_state.mouse.buttons = state->buttons;
        changed = true;
    }

    if (changed)
    {
        hid_device_set_mouse_state(g_device, &g_remote_state.mouse);
        hid_device_request_notify(g_device, true, false, false);
    }
}

static void on_keyboard_input(const keyboard_state_t *state)
{
    if (!state)
        return;

    bool changed = false;

    if (state->modifiers != g_remote_state.keyboard.modifiers)
    {
        g_remote_state.keyboard.modifiers = state->modifiers;
        changed = true;
    }

    if (memcmp(state->keys, g_remote_state.keyboard.keys, 6) != 0)
    {
        memcpy(g_remote_state.keyboard.keys, state->keys, 6);
        changed = true;
    }

    if (changed)
    {
        hid_device_set_keyboard_state(g_device, &g_remote_state.keyboard);
        hid_device_request_notify(g_device, false, true, false);
    }
}

static void on_consumer_input(const consumer_state_t *state)
{
    if (!state || !g_device)
        return;

    consumer_state_t normalized = *state;

    uint16_t raw_usage = normalized.usage;
    uint16_t usage_mask = ble_hid_consumer_usage_to_mask(raw_usage);
    if (raw_usage != 0 && usage_mask == 0)
    {
        ESP_LOGW(TAG, "Unsupported consumer usage request: 0x%04X", raw_usage);
        return;
    }
    normalized.usage = usage_mask;

    if (normalized.active && normalized.usage == 0)
    {
        return;
    }

    if (!normalized.active && normalized.usage == 0)
    {
        g_remote_state.consumer.active = false;
        g_remote_state.consumer.usage = 0;
        g_remote_state.consumer.hold = false;
        hid_device_set_consumer_state(g_device, &g_remote_state.consumer);
        hid_device_request_notify(g_device, false, false, true);
        return;
    }

    g_remote_state.consumer = normalized;
    hid_device_set_consumer_state(g_device, &g_remote_state.consumer);
    hid_device_request_notify(g_device, false, false, true);
}

static void send_control_response(cJSON *response)
{
    if (!response)
        return;

    char *json_str = cJSON_PrintUnformatted(response);
    if (json_str)
    {
        transport_uart_send(json_str);
        transport_websocket_send(json_str);
        free(json_str);
    }
}

static void on_control_message(cJSON *msg)
{
    if (!msg)
        return;

    cJSON *cmd_item = cJSON_GetObjectItem(msg, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item))
    {
        return;
    }

    const char *cmd = cmd_item->valuestring;
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "control_response");
    cJSON_AddStringToObject(response, "cmd", cmd);

    if (strcmp(cmd, "force_adv") == 0)
    {
        g_advertising_enabled = true;
        esp_err_t err = hid_device_start_advertising(g_device);
        cJSON_AddBoolToObject(response, "ok", err == ESP_OK);
        if (err != ESP_OK)
        {
            cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
        }
    }
    else if (strcmp(cmd, "quiet") == 0)
    {
        g_advertising_enabled = false;
        esp_err_t err = hid_device_stop_advertising(g_device);
        cJSON_AddBoolToObject(response, "ok", err == ESP_OK);
        if (err != ESP_OK)
        {
            cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
        }
    }
    else if (strcmp(cmd, "forget") == 0)
    {
        esp_err_t err = hid_device_forget_peer(g_device);
        cJSON_AddBoolToObject(response, "ok", err == ESP_OK);
        if (err == ESP_OK)
        {
            g_advertising_enabled = true;
            hid_device_start_advertising(g_device);
            broadcast_ble_status();
        }
        else
        {
            cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
        }
    }
    else if (strcmp(cmd, "wifi_get") == 0)
    {
        cJSON *wifi_obj = cJSON_CreateObject();
        if (wifi_obj)
        {
            populate_wifi_status_json(wifi_obj);

            // Load saved credentials
            char ssid[33] = {0};
            char pass[64] = {0};
            cJSON *creds = cJSON_CreateObject();
            if (creds)
            {
                if (wifi_manager_load_config(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK)
                {
                    cJSON_AddStringToObject(creds, "ssid", ssid);
                    cJSON_AddBoolToObject(creds, "has_psk", strlen(pass) > 0);
                }
                else
                {
                    cJSON_AddStringToObject(creds, "ssid", "");
                    cJSON_AddBoolToObject(creds, "has_psk", false);
                }
                cJSON_AddItemToObject(wifi_obj, "creds", creds);
            }

            cJSON_AddItemToObject(response, "wifi", wifi_obj);
        }
        cJSON_AddBoolToObject(response, "ok", true);
    }
    else if (strcmp(cmd, "wifi_set") == 0)
    {
        cJSON *ssid_item = cJSON_GetObjectItem(msg, "ssid");
        cJSON *psk_item = cJSON_GetObjectItem(msg, "psk");
        cJSON *apply_item = cJSON_GetObjectItem(msg, "apply");

        if (ssid_item && cJSON_IsString(ssid_item))
        {
            const char *ssid = ssid_item->valuestring;
            const char *psk = (psk_item && cJSON_IsString(psk_item)) ? psk_item->valuestring : "";

            esp_err_t err = wifi_manager_save_config(ssid, psk);
            if (err == ESP_OK)
            {
                bool apply = !apply_item || cJSON_IsTrue(apply_item);
                bool status_changed = false;

                if (apply)
                {
                    wifi_manager_stop();
                    vTaskDelay(pdMS_TO_TICKS(100));
                    err = wifi_manager_start_sta(ssid, psk);
                    status_changed = (err == ESP_OK);

                    // Wait a bit for connection
                    for (int i = 0; err == ESP_OK && i < 80 && !wifi_manager_is_connected(); i++)
                    {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }

                    if (err == ESP_OK && wifi_manager_is_connected())
                    {
                        char ip[16] = {0};
                        wifi_manager_get_ip(ip, sizeof(ip));
                        cJSON_AddStringToObject(response, "mode", "sta");
                        cJSON_AddStringToObject(response, "ip", ip);
                    }
                    else
                    {
                        // Fallback to AP mode
                        wifi_manager_stop();
                        wifi_manager_start_ap(NULL, DEFAULT_AP_PASS);
                        cJSON_AddStringToObject(response, "mode", "ap");
                        cJSON_AddStringToObject(response, "ip", "192.168.4.1");
                        status_changed = true;
                    }
                }

                cJSON_AddBoolToObject(response, "ok", true);

        if (status_changed)
        {
            notify_wifi_status();
        }
    }
            else
            {
                cJSON_AddBoolToObject(response, "ok", false);
                cJSON_AddStringToObject(response, "err", "write_failed");
            }
        }
        else
        {
            cJSON_AddBoolToObject(response, "ok", false);
            cJSON_AddStringToObject(response, "err", "missing_ssid");
        }
    }
    else if (strcmp(cmd, "wifi_scan") == 0)
    {
        esp_err_t err = wifi_manager_start_scan(http_server_publish_scan_results);
        cJSON_AddBoolToObject(response, "ok", err == ESP_OK);
        if (err != ESP_OK)
        {
            cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
        }
        else
        {
            notify_wifi_status();
        }
    }
    else if (strcmp(cmd, "wifi_clear") == 0)
    {
        esp_err_t err = wifi_manager_clear_config();
        if (err == ESP_OK)
        {
            wifi_manager_stop();
            wifi_manager_start_ap(NULL, DEFAULT_AP_PASS);
            cJSON_AddBoolToObject(response, "ok", true);
            notify_wifi_status();
            broadcast_ble_status();
        }
        else
        {
            cJSON_AddBoolToObject(response, "ok", false);
            cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
        }
    }
    else
    {
        cJSON_AddBoolToObject(response, "ok", false);
        cJSON_AddStringToObject(response, "err", "unknown_cmd");
    }

    send_control_response(response);
    cJSON_Delete(response);
}

static void notify_timer_callback(TimerHandle_t timer)
{
    hid_device_state_t state = hid_device_get_state(g_device);

    if (state != DEVICE_STATE_CONNECTED)
    {
        return;
    }

    bool updated = false;

    if (!g_remote_state.mouse_hold &&
        (g_remote_state.mouse.x != 0 || g_remote_state.mouse.y != 0))
    {
        g_remote_state.mouse.x = 0;
        g_remote_state.mouse.y = 0;
        hid_device_set_mouse_state(g_device, &g_remote_state.mouse);
        updated = true;
    }

    if (!g_remote_state.wheel_hold && g_remote_state.mouse.wheel != 0)
    {
        g_remote_state.mouse.wheel = 0;
        hid_device_set_mouse_state(g_device, &g_remote_state.mouse);
        updated = true;
    }

    if (updated)
    {
        hid_device_request_notify(g_device, true, false, false);
    }
}

void app_main(void)
{
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    ESP_LOGI(TAG, "Composite HID Device starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create HID device
    g_device = hid_device_create("Composite HID");
    if (!g_device)
    {
        ESP_LOGE(TAG, "Failed to create HID device");
        return;
    }

    hid_device_set_state_callback(g_device, device_state_changed);

    // Start HID device
    if (hid_device_start(g_device) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HID device");
        return;
    }

    broadcast_ble_status();

    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_manager_init());

    // Start HTTP server (serves UI and captive portal)
    esp_err_t http_err = http_server_start(DEFAULT_HTTP_PORT);
    if (http_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(http_err));
    }

    // Try to load and connect to saved WiFi
    char ssid[33] = {0};
    char pass[64] = {0};
    if (wifi_manager_load_config(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK)
    {
        ESP_LOGI(TAG, "Connecting to saved WiFi: %s", ssid);
        wifi_manager_start_sta(ssid, pass);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    // If not connected, start AP mode
    if (!wifi_manager_is_connected())
    {
        wifi_manager_start_ap(NULL, DEFAULT_AP_PASS);
        ESP_LOGI(TAG, "AP mode active: %s", wifi_manager_get_ap_ssid());
    }

    // Initialize transports
    transport_callbacks_t callbacks = {
        .on_mouse = on_mouse_input,
        .on_keyboard = on_keyboard_input,
        .on_consumer = on_consumer_input,
        .on_control = on_control_message};

    ESP_ERROR_CHECK(transport_uart_init(&callbacks));

    // Wait a bit for WiFi to stabilize
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_err_t ws_err = transport_websocket_init(&callbacks, DEFAULT_WS_PORT);
    if (ws_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WebSocket transport: %s", esp_err_to_name(ws_err));
    }
    else
    {
        notify_wifi_status();
    }

    // Create notification timer
    g_notify_timer = xTimerCreate("notify", pdMS_TO_TICKS(NOTIFY_INTERVAL_MS),
                                  pdTRUE, NULL, notify_timer_callback);
    xTimerStart(g_notify_timer, 0);

    ESP_LOGI(TAG, "System ready!");

    // Log connection info
    char ip[16] = {0};
    if (wifi_manager_get_ip(ip, sizeof(ip)) == ESP_OK)
    {
        ESP_LOGI(TAG, "IP Address: %s", ip);
        ESP_LOGI(TAG, "WebSocket: ws://%s:%d/ws", ip, DEFAULT_WS_PORT);
    }
}
