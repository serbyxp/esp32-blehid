#include "transport_ws.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "http_server.h"
#include "ws_ascii.h"
#include "ble_hid.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WS_TRANSPORT";

#define WS_MAX_CLIENTS 4
#define WS_ASCII_QUEUE_LEN 64
#define WS_ASCII_COMBO_STAGE_DELAY_MS 8
#define WS_ASCII_RELEASE_DELAY_MS 12
#define WS_ASCII_INTERCHAR_DELAY_MS 6
#define WS_ASCII_SENTINEL 0xFFFF

static httpd_handle_t s_server = NULL;
static transport_callbacks_t s_callbacks = {0};
static QueueHandle_t s_ascii_queue = NULL;
static TaskHandle_t s_ascii_task = NULL;

typedef struct
{
    int fd;
    bool active;
} ws_client_t;

static ws_client_t s_clients[WS_MAX_CLIENTS] = {0};

static void process_ws_message(const char *data, size_t len);
static void register_client(int fd);
static void unregister_client(int fd);
static int httpd_req_to_client_fd(httpd_req_t *req);
static void ws_send_keyboard_state(const keyboard_state_t *state);
static void ws_send_ascii_char(uint8_t ascii);
static void ws_send_ascii_text(const char *text);
static void ws_ascii_task(void *arg);

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_client_fd(req);

    if (req->method == HTTP_GET)
    {
        register_client(fd);
        ESP_LOGI(TAG, "WebSocket client connected (fd=%d)", fd);
        http_server_publish_wifi_status();
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
        unregister_client(fd);
        return ret;
    }

    if (ws_pkt.len == 0)
    {
        if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
        {
            unregister_client(fd);
        }
        return ESP_OK;
    }

    uint8_t *buf = calloc(1, ws_pkt.len + 1);
    if (buf == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for ws buffer");
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
        free(buf);
        unregister_client(fd);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
    {
        unregister_client(fd);
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
    {
        buf[ws_pkt.len] = '\0';
        process_ws_message((char *)buf, ws_pkt.len);
    }

    free(buf);
    return ESP_OK;
}

static void process_ws_message(const char *data, size_t len)
{
    cJSON *json = cJSON_Parse(data);
    if (!json)
    {
        ESP_LOGW(TAG, "Failed to parse JSON");
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    if (!type_item || !cJSON_IsString(type_item))
    {
        cJSON_Delete(json);
        return;
    }

    const char *type = type_item->valuestring;

    if (strcmp(type, "mouse") == 0 && s_callbacks.on_mouse)
    {
        mouse_state_t state = {0};

        cJSON *dx = cJSON_GetObjectItem(json, "dx");
        cJSON *dy = cJSON_GetObjectItem(json, "dy");
        cJSON *wheel = cJSON_GetObjectItem(json, "wheel");
        cJSON *hwheel = cJSON_GetObjectItem(json, "hwheel");
        cJSON *buttons = cJSON_GetObjectItem(json, "buttons");

        if (dx && cJSON_IsNumber(dx))
            state.x = (int8_t)dx->valueint;
        if (dy && cJSON_IsNumber(dy))
            state.y = (int8_t)dy->valueint;
        if (wheel && cJSON_IsNumber(wheel))
            state.wheel = (int8_t)wheel->valueint;
        if (hwheel && cJSON_IsNumber(hwheel))
            state.hwheel = (int8_t)hwheel->valueint;

        if (buttons && cJSON_IsObject(buttons))
        {
            cJSON *left = cJSON_GetObjectItem(buttons, "left");
            cJSON *right = cJSON_GetObjectItem(buttons, "right");
            cJSON *middle = cJSON_GetObjectItem(buttons, "middle");
            cJSON *back = cJSON_GetObjectItem(buttons, "back");
            cJSON *forward = cJSON_GetObjectItem(buttons, "forward");

            state.buttons = 0;
            if (left && cJSON_IsTrue(left))
                state.buttons |= 0x01;
            if (right && cJSON_IsTrue(right))
                state.buttons |= 0x02;
            if (middle && cJSON_IsTrue(middle))
                state.buttons |= 0x04;
            if (back && cJSON_IsTrue(back))
                state.buttons |= 0x08;
            if (forward && cJSON_IsTrue(forward))
                state.buttons |= 0x10;
        }

        s_callbacks.on_mouse(&state);
    }
    else if (strcmp(type, "keyboard") == 0)
    {
        cJSON *text_item = cJSON_GetObjectItem(json, "text");
        if (text_item && cJSON_IsString(text_item))
        {
            ws_send_ascii_text(text_item->valuestring);
            cJSON_Delete(json);
            return;
        }

        cJSON *ascii_item = cJSON_GetObjectItem(json, "ascii");
        if (ascii_item && cJSON_IsNumber(ascii_item))
        {
            ws_send_ascii_char((uint8_t)ascii_item->valueint);
            cJSON_Delete(json);
            return;
        }

        if (!s_callbacks.on_keyboard)
        {
            cJSON_Delete(json);
            return;
        }

        keyboard_state_t state = {0};

        cJSON *modifiers = cJSON_GetObjectItem(json, "modifiers");
        if (modifiers && cJSON_IsObject(modifiers))
        {
            uint8_t mods = 0;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "left_control")))
                mods |= 0x01;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "left_shift")))
                mods |= 0x02;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "left_alt")))
                mods |= 0x04;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "left_gui")))
                mods |= 0x08;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "right_control")))
                mods |= 0x10;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "right_shift")))
                mods |= 0x20;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "right_alt")))
                mods |= 0x40;
            if (cJSON_IsTrue(cJSON_GetObjectItem(modifiers, "right_gui")))
                mods |= 0x80;
            state.modifiers = mods;
        }

        cJSON *keys = cJSON_GetObjectItem(json, "keys");
        if (keys && cJSON_IsArray(keys))
        {
            int count = cJSON_GetArraySize(keys);
            for (int i = 0; i < count && i < 6; i++)
            {
                cJSON *key = cJSON_GetArrayItem(keys, i);
                if (cJSON_IsNumber(key))
                {
                    state.keys[i] = (uint8_t)key->valueint;
                }
            }
        }

        s_callbacks.on_keyboard(&state);
    }
    else if (strcmp(type, "consumer") == 0 && s_callbacks.on_consumer)
    {
        consumer_state_t state = {
            .usage = 0,
            .active = true,
            .hold = false};

        cJSON *usage_item = cJSON_GetObjectItem(json, "usage");
        cJSON *pressed_item = cJSON_GetObjectItem(json, "pressed");
        cJSON *hold_item = cJSON_GetObjectItem(json, "hold");

        if (usage_item && cJSON_IsNumber(usage_item))
        {
            int value = usage_item->valueint;
            if (value < 0)
            {
                value = 0;
            }
            else if (value > 0xFFFF)
            {
                value = 0xFFFF;
            }

            uint16_t raw_usage = (uint16_t)value;
            uint16_t mask = ble_hid_consumer_usage_to_mask(raw_usage);

            if (raw_usage != 0 && mask == 0)
            {
                ESP_LOGW(TAG, "Unsupported consumer usage from WS: 0x%04X", raw_usage);
                state.active = false;
                state.hold = false;
                state.usage = 0;
            }
            else
            {
                state.usage = mask;
            }
        }

        if (pressed_item)
        {
            state.active = cJSON_IsTrue(pressed_item);
        }

        if (hold_item)
        {
            state.hold = cJSON_IsTrue(hold_item);
        }

        s_callbacks.on_consumer(&state);
    }
    else if (strcmp(type, "control") == 0 && s_callbacks.on_control)
    {
        s_callbacks.on_control(json);
    }

    cJSON_Delete(json);
}

static void ws_send_keyboard_state(const keyboard_state_t *state)
{
    if (!s_callbacks.on_keyboard || !state)
    {
        return;
    }

    s_callbacks.on_keyboard(state);
}

static void ws_emit_ascii_reports(const keyboard_state_t *reports, size_t count)
{
    if (!reports || count == 0)
    {
        return;
    }

    bool is_combo_sequence =
        (count == WS_ASCII_REPORT_COUNT) && (reports[0].modifiers != 0);
    const TickType_t combo_delays[] = {
        pdMS_TO_TICKS(WS_ASCII_COMBO_STAGE_DELAY_MS),
        pdMS_TO_TICKS(WS_ASCII_COMBO_STAGE_DELAY_MS),
        pdMS_TO_TICKS(WS_ASCII_RELEASE_DELAY_MS),
    };

    for (size_t i = 0; i < count; ++i)
    {
        ws_send_keyboard_state(&reports[i]);

        TickType_t delay = pdMS_TO_TICKS(WS_ASCII_INTERCHAR_DELAY_MS);
        if (i + 1 < count)
        {
            if (is_combo_sequence && i < (sizeof(combo_delays) / sizeof(combo_delays[0])))
            {
                delay = combo_delays[i];
            }
            else
            {
                delay = pdMS_TO_TICKS(WS_ASCII_RELEASE_DELAY_MS);
            }
        }

        vTaskDelay(delay);
    }
}

static void ws_send_ascii_char(uint8_t ascii)
{
    if (!s_callbacks.on_keyboard)
    {
        return;
    }

    if (s_ascii_queue)
    {
        uint16_t value = ascii;
        if (xQueueSend(s_ascii_queue, &value, pdMS_TO_TICKS(50)) != pdPASS)
        {
            ESP_LOGW(TAG, "ASCII queue full, dropping char %u", ascii);
        }
        return;
    }

    keyboard_state_t reports[WS_ASCII_REPORT_COUNT] = {0};
    size_t report_count = 0;
    if (!ws_ascii_prepare_reports(ascii, reports, &report_count) || report_count == 0)
    {
        ESP_LOGW(TAG, "Unsupported ASCII character: %u", ascii);
        return;
    }

    ws_emit_ascii_reports(reports, report_count);
}

static void ws_send_ascii_text(const char *text)
{
    if (!text)
    {
        return;
    }

    while (*text)
    {
        ws_send_ascii_char((uint8_t)*text++);
    }
}

static void ws_ascii_task(void *arg)
{
    uint16_t ascii = 0;

    while (true)
    {
        if (xQueueReceive(s_ascii_queue, &ascii, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if (ascii == WS_ASCII_SENTINEL)
        {
            break;
        }

        if (!s_callbacks.on_keyboard)
        {
            continue;
        }

        keyboard_state_t reports[WS_ASCII_REPORT_COUNT] = {0};
        size_t report_count = 0;
        if (!ws_ascii_prepare_reports((uint8_t)ascii, reports, &report_count) || report_count == 0)
        {
            ESP_LOGW(TAG, "Unsupported ASCII character: %u", ascii);
            continue;
        }

        ws_emit_ascii_reports(reports, report_count);
    }

    s_ascii_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t transport_websocket_init(const transport_callbacks_t *callbacks, uint16_t port)
{
    if (!callbacks)
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_callbacks = *callbacks;
    for (int i = 0; i < WS_MAX_CLIENTS; ++i)
    {
        s_clients[i].fd = -1;
        s_clients[i].active = false;
    }

    if (!s_ascii_queue)
    {
        s_ascii_queue = xQueueCreate(WS_ASCII_QUEUE_LEN, sizeof(uint16_t));
        if (!s_ascii_queue)
        {
            ESP_LOGE(TAG, "Failed to create ASCII queue");
        }
    }

    if (s_ascii_queue && !s_ascii_task)
    {
        if (xTaskCreate(ws_ascii_task, "ws_ascii", 3072, NULL, tskIDLE_PRIORITY + 2, &s_ascii_task) != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to start ASCII task");
            vQueueDelete(s_ascii_queue);
            s_ascii_queue = NULL;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.ctrl_port = port + 1;
    config.max_open_sockets = WS_MAX_CLIENTS + 2;

    if (httpd_start(&s_server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true};

    httpd_register_uri_handler(s_server, &ws_uri);

    ESP_LOGI(TAG, "WebSocket server started on port %d", port);
    return ESP_OK;
}

esp_err_t transport_websocket_deinit(void)
{
    if (s_ascii_queue)
    {
        if (s_ascii_task)
        {
            uint16_t sentinel = WS_ASCII_SENTINEL;
            xQueueSend(s_ascii_queue, &sentinel, portMAX_DELAY);
            for (int i = 0; i < 10 && s_ascii_task; ++i)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        vQueueDelete(s_ascii_queue);
        s_ascii_queue = NULL;
    }

    if (s_server)
    {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "WebSocket server stopped");
    }
    return ESP_OK;
}

esp_err_t transport_websocket_send(const char *message)
{
    if (!s_server || !message)
    {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)message;
    ws_pkt.len = strlen(message);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Broadcast to all connected clients
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
    {
        if (s_clients[i].active)
        {
            esp_err_t err = httpd_ws_send_frame_async(s_server, s_clients[i].fd, &ws_pkt);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to send to fd %d: %s", s_clients[i].fd, esp_err_to_name(err));
                unregister_client(s_clients[i].fd);
            }
        }
    }

    return ESP_OK;
}

static int httpd_req_to_client_fd(httpd_req_t *req)
{
    return httpd_req_to_sockfd(req);
}

static void register_client(int fd)
{
    if (fd < 0)
    {
        return;
    }

    for (int i = 0; i < WS_MAX_CLIENTS; ++i)
    {
        if (s_clients[i].active && s_clients[i].fd == fd)
        {
            return;
        }
    }

    for (int i = 0; i < WS_MAX_CLIENTS; ++i)
    {
        if (!s_clients[i].active)
        {
            s_clients[i].fd = fd;
            s_clients[i].active = true;
            return;
        }
    }

    ESP_LOGW(TAG, "No free slots for new WebSocket client (fd=%d)", fd);
}

static void unregister_client(int fd)
{
    if (fd < 0)
    {
        return;
    }

    for (int i = 0; i < WS_MAX_CLIENTS; ++i)
    {
        if (s_clients[i].active && s_clients[i].fd == fd)
        {
            ESP_LOGI(TAG, "WebSocket client disconnected (fd=%d)", fd);
            s_clients[i].active = false;
            s_clients[i].fd = -1;
            break;
        }
    }
}
