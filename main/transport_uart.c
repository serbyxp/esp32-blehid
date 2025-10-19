#include "transport_uart.h"
#include "transport_uart.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "hid_keymap.h"
#include "ble_hid.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "UART_TRANSPORT";

#define UART_NUM UART_NUM_0
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 1024
#define UART_RX_BUF (UART_BUF_SIZE * 2)
#define UART_TX_BUF (UART_BUF_SIZE * 2)

static transport_callbacks_t s_callbacks = {0};
static bool s_running = false;
static char s_rx_buffer[UART_BUF_SIZE];

static void uart_event_task(void *arg);
static void uart_send_keyboard_press(uint8_t keycode, uint8_t modifiers);
static void uart_send_keyboard_release(void);
static void uart_send_ascii_char(uint8_t ascii);
static void uart_send_ascii_text(const char *text);

esp_err_t transport_uart_init(const transport_callbacks_t *callbacks)
{
    if (!callbacks)
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_callbacks = *callbacks;

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));

    s_running = true;
    xTaskCreate(uart_event_task, "uart_task", 4096, NULL, 10, NULL);

    ESP_LOGI(TAG, "UART transport initialized on UART%d @ %d baud", UART_NUM, UART_BAUD_RATE);
    return ESP_OK;
}

esp_err_t transport_uart_deinit(void)
{
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_driver_delete(UART_NUM);
    ESP_LOGI(TAG, "UART transport deinitialized");
    return ESP_OK;
}

esp_err_t transport_uart_send(const char *message)
{
    if (!s_running || !message)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int len = strlen(message);
    int written = uart_write_bytes(UART_NUM, message, len);
    uart_write_bytes(UART_NUM, "\n", 1);

    return (written == len) ? ESP_OK : ESP_FAIL;
}

static void uart_send_keyboard_press(uint8_t keycode, uint8_t modifiers)
{
    if (!s_callbacks.on_keyboard)
    {
        return;
    }

    keyboard_state_t state = {0};
    state.modifiers = modifiers;
    state.keys[0] = keycode;
    s_callbacks.on_keyboard(&state);
}

static void uart_send_keyboard_release(void)
{
    if (!s_callbacks.on_keyboard)
    {
        return;
    }

    keyboard_state_t state = {0};
    s_callbacks.on_keyboard(&state);
}

static void uart_send_ascii_char(uint8_t ascii)
{
    uint8_t keycode = 0;
    uint8_t modifiers = 0;
    if (!hid_keymap_from_ascii(ascii, &keycode, &modifiers))
    {
        ESP_LOGW(TAG, "Unsupported ASCII character: %u", ascii);
        return;
    }

    uart_send_keyboard_press(keycode, modifiers);
    uart_send_keyboard_release();
}

static void uart_send_ascii_text(const char *text)
{
    if (!text)
    {
        return;
    }

    while (*text)
    {
        uart_send_ascii_char((uint8_t)*text++);
    }
}

static void process_message(const char *line)
{
    cJSON *json = cJSON_Parse(line);
    if (!json)
    {
        ESP_LOGW(TAG, "Failed to parse JSON: %s", line);
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
        cJSON *buttons = cJSON_GetObjectItem(json, "buttons");

        if (dx && cJSON_IsNumber(dx))
            state.x = (int8_t)dx->valueint;
        if (dy && cJSON_IsNumber(dy))
            state.y = (int8_t)dy->valueint;
        if (wheel && cJSON_IsNumber(wheel))
            state.wheel = (int8_t)wheel->valueint;

        if (buttons && cJSON_IsObject(buttons))
        {
            cJSON *left = cJSON_GetObjectItem(buttons, "left");
            cJSON *right = cJSON_GetObjectItem(buttons, "right");
            cJSON *middle = cJSON_GetObjectItem(buttons, "middle");

            state.buttons = 0;
            if (left && cJSON_IsTrue(left))
                state.buttons |= 0x01;
            if (right && cJSON_IsTrue(right))
                state.buttons |= 0x02;
            if (middle && cJSON_IsTrue(middle))
                state.buttons |= 0x04;
        }

        s_callbacks.on_mouse(&state);
    }
    else if (strcmp(type, "keyboard") == 0 && s_callbacks.on_keyboard)
    {
        cJSON *text_item = cJSON_GetObjectItem(json, "text");
        if (text_item && cJSON_IsString(text_item))
        {
            uart_send_ascii_text(text_item->valuestring);
            cJSON_Delete(json);
            return;
        }

        cJSON *ascii_item = cJSON_GetObjectItem(json, "ascii");
        if (ascii_item && cJSON_IsNumber(ascii_item))
        {
            uart_send_ascii_char((uint8_t)ascii_item->valueint);
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
                ESP_LOGW(TAG, "Unsupported consumer usage from UART: 0x%04X", raw_usage);
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

static void uart_event_task(void *arg)
{
    int pos = 0;

    while (s_running)
    {
        int len = uart_read_bytes(UART_NUM, (uint8_t *)(s_rx_buffer + pos),
                                  UART_BUF_SIZE - pos - 1, pdMS_TO_TICKS(100));

        if (len > 0)
        {
            pos += len;
            s_rx_buffer[pos] = '\0';

            // Process complete lines
            char *line_start = s_rx_buffer;
            char *newline;

            while ((newline = strchr(line_start, '\n')) != NULL)
            {
                *newline = '\0';

                // Remove carriage return if present
                if (newline > line_start && *(newline - 1) == '\r')
                {
                    *(newline - 1) = '\0';
                }

                if (strlen(line_start) > 0)
                {
                    process_message(line_start);
                }

                line_start = newline + 1;
            }

            // Move remaining data to start of buffer
            if (line_start > s_rx_buffer)
            {
                int remaining = s_rx_buffer + pos - line_start;
                if (remaining > 0)
                {
                    memmove(s_rx_buffer, line_start, remaining);
                    pos = remaining;
                }
                else
                {
                    pos = 0;
                }
            }

            // Buffer overflow protection
            if (pos >= UART_BUF_SIZE - 1)
            {
                ESP_LOGW(TAG, "Buffer overflow, clearing");
                pos = 0;
            }
        }
    }

    vTaskDelete(NULL);
}
