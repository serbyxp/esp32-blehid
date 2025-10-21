#include "hid_device.h"
#include "ble_hid.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HID_DEVICE";

#define HID_FLUSH_RETRY_DELAY_MS 30

typedef struct
{
    bool mouse;
    bool keyboard;
    bool consumer;
} hid_flush_retry_request_t;

static void hid_device_schedule_retry(hid_device_t *device, bool mouse, bool keyboard, bool consumer);
static void hid_device_retry_timer_callback(TimerHandle_t timer);

struct hid_device_s
{
    char device_name[32];
    device_state_t state;
    hid_device_state_t ble_state;
    state_change_callback_t callback;
};

static void internal_state_callback(hid_device_state_t state);
static void hid_device_flush_reports(hid_device_t *device, bool mouse, bool keyboard, bool consumer);
static hid_device_t *g_device = NULL;
static hid_flush_retry_request_t s_pending_retry = {0};
static TimerHandle_t s_retry_timer = NULL;
static StaticTimer_t s_retry_timer_buffer;
static portMUX_TYPE s_retry_lock = portMUX_INITIALIZER_UNLOCKED;

static bool mouse_states_equal(const mouse_state_t *a, const mouse_state_t *b)
{
    return a->x == b->x && a->y == b->y && a->wheel == b->wheel &&
           a->hwheel == b->hwheel && a->buttons == b->buttons;
}

static void hid_device_mouse_queue_push(device_state_t *state, const mouse_state_t *value)
{
    if (!state || !value)
    {
        return;
    }

    if (state->mouse_queue.count > 0)
    {
        size_t last_index = (state->mouse_queue.head + state->mouse_queue.count - 1) %
                            HID_MOUSE_QUEUE_DEPTH;
        if (mouse_states_equal(&state->mouse_queue.entries[last_index], value))
        {
            return;
        }
    }

    if (state->mouse_queue.count == HID_MOUSE_QUEUE_DEPTH)
    {
        ESP_LOGW(TAG, "Mouse queue full, dropping oldest report");
        state->mouse_queue.head = (state->mouse_queue.head + 1) % HID_MOUSE_QUEUE_DEPTH;
        state->mouse_queue.count--;
    }

    size_t tail = (state->mouse_queue.head + state->mouse_queue.count) % HID_MOUSE_QUEUE_DEPTH;
    state->mouse_queue.entries[tail] = *value;
    state->mouse_queue.count++;
    state->mouse_updated = true;
}

static mouse_state_t *hid_device_mouse_queue_peek(device_state_t *state)
{
    if (!state || state->mouse_queue.count == 0)
    {
        return NULL;
    }

    return &state->mouse_queue.entries[state->mouse_queue.head];
}

static void hid_device_mouse_queue_pop(device_state_t *state)
{
    if (!state || state->mouse_queue.count == 0)
    {
        return;
    }

    state->mouse_queue.head = (state->mouse_queue.head + 1) % HID_MOUSE_QUEUE_DEPTH;
    state->mouse_queue.count--;
    state->mouse_updated = (state->mouse_queue.count > 0);
}

static bool keyboard_states_equal(const keyboard_state_t *a, const keyboard_state_t *b)
{
    return a->modifiers == b->modifiers && a->reserved == b->reserved &&
           memcmp(a->keys, b->keys, sizeof(a->keys)) == 0;
}

static void hid_device_keyboard_queue_push(device_state_t *state, const keyboard_state_t *value)
{
    if (!state || !value)
    {
        return;
    }

    if (state->keyboard_queue.count > 0)
    {
        size_t last_index = (state->keyboard_queue.head + state->keyboard_queue.count - 1) %
                            HID_KEYBOARD_QUEUE_DEPTH;
        if (keyboard_states_equal(&state->keyboard_queue.entries[last_index], value))
        {
            return;
        }
    }

    if (state->keyboard_queue.count == HID_KEYBOARD_QUEUE_DEPTH)
    {
        ESP_LOGW(TAG, "Keyboard queue full, dropping oldest report");
        state->keyboard_queue.head = (state->keyboard_queue.head + 1) % HID_KEYBOARD_QUEUE_DEPTH;
        state->keyboard_queue.count--;
    }

    size_t tail = (state->keyboard_queue.head + state->keyboard_queue.count) %
                  HID_KEYBOARD_QUEUE_DEPTH;
    state->keyboard_queue.entries[tail] = *value;
    state->keyboard_queue.count++;
    state->keyboard_updated = true;
}

static keyboard_state_t *hid_device_keyboard_queue_peek(device_state_t *state)
{
    if (!state || state->keyboard_queue.count == 0)
    {
        return NULL;
    }

    return &state->keyboard_queue.entries[state->keyboard_queue.head];
}

static void hid_device_keyboard_queue_pop(device_state_t *state)
{
    if (!state || state->keyboard_queue.count == 0)
    {
        return;
    }

    state->keyboard_queue.head = (state->keyboard_queue.head + 1) % HID_KEYBOARD_QUEUE_DEPTH;
    state->keyboard_queue.count--;
    state->keyboard_updated = (state->keyboard_queue.count > 0);
}

static void hid_device_consumer_queue_push(device_state_t *state, uint16_t usage)
{
    if (!state)
    {
        return;
    }

    if (state->consumer_queue.count > 0)
    {
        size_t last_index = (state->consumer_queue.head + state->consumer_queue.count - 1) %
                            HID_CONSUMER_QUEUE_DEPTH;
        if (state->consumer_queue.entries[last_index] == usage)
        {
            return;
        }
    }

    if (state->consumer_queue.count == HID_CONSUMER_QUEUE_DEPTH)
    {
        ESP_LOGW(TAG, "Consumer queue full, dropping oldest report");
        state->consumer_queue.head = (state->consumer_queue.head + 1) % HID_CONSUMER_QUEUE_DEPTH;
        state->consumer_queue.count--;
    }

    size_t tail = (state->consumer_queue.head + state->consumer_queue.count) %
                  HID_CONSUMER_QUEUE_DEPTH;
    state->consumer_queue.entries[tail] = usage;
    state->consumer_queue.count++;
    state->consumer_updated = true;
}

static uint16_t *hid_device_consumer_queue_peek(device_state_t *state)
{
    if (!state || state->consumer_queue.count == 0)
    {
        return NULL;
    }

    return &state->consumer_queue.entries[state->consumer_queue.head];
}

static void hid_device_consumer_queue_pop(device_state_t *state)
{
    if (!state || state->consumer_queue.count == 0)
    {
        return;
    }

    state->consumer_queue.head = (state->consumer_queue.head + 1) % HID_CONSUMER_QUEUE_DEPTH;
    state->consumer_queue.count--;
    state->consumer_updated = (state->consumer_queue.count > 0);
}

hid_device_t *hid_device_create(const char *device_name)
{
    hid_device_t *device = calloc(1, sizeof(hid_device_t));
    if (!device)
    {
        ESP_LOGE(TAG, "Failed to allocate device");
        return NULL;
    }

    if (device_name)
    {
        strncpy(device->device_name, device_name, sizeof(device->device_name) - 1);
    }
    else
    {
        strcpy(device->device_name, "Composite HID");
    }

    device->ble_state = DEVICE_STATE_STOPPED;
    g_device = device;

    ESP_LOGI(TAG, "Device created: %s", device->device_name);
    return device;
}

void hid_device_destroy(hid_device_t *device)
{
    if (device)
    {
        if (device->ble_state != DEVICE_STATE_STOPPED)
        {
            hid_device_stop(device);
        }
        if (s_retry_timer)
        {
            xTimerStop(s_retry_timer, 0);
            vTimerSetTimerID(s_retry_timer, NULL);
        }
        portENTER_CRITICAL(&s_retry_lock);
        s_pending_retry.mouse = false;
        s_pending_retry.keyboard = false;
        s_pending_retry.consumer = false;
        portEXIT_CRITICAL(&s_retry_lock);
        free(device);
        g_device = NULL;
    }
}

esp_err_t hid_device_start(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (device->ble_state != DEVICE_STATE_STOPPED)
    {
        ESP_LOGW(TAG, "Device already started");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ble_hid_init(device->device_name);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize BLE HID");
        return err;
    }

    ble_hid_set_state_callback(internal_state_callback);
    device->ble_state = DEVICE_STATE_IDLE;

    if (device->callback)
    {
        device->callback(device->ble_state);
    }

    ESP_LOGI(TAG, "Device started");
    return ESP_OK;
}

esp_err_t hid_device_stop(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (device->ble_state == DEVICE_STATE_ADVERTISING)
    {
        ble_hid_stop_advertising();
    }

    ble_hid_deinit();
    device->ble_state = DEVICE_STATE_STOPPED;

    if (device->callback)
    {
        device->callback(device->ble_state);
    }

    ESP_LOGI(TAG, "Device stopped");
    return ESP_OK;
}

hid_device_state_t hid_device_get_state(hid_device_t *device)
{
    return device ? device->ble_state : DEVICE_STATE_STOPPED;
}

void hid_device_set_state_callback(hid_device_t *device, state_change_callback_t callback)
{
    if (device)
    {
        device->callback = callback;
    }
}

esp_err_t hid_device_start_advertising(hid_device_t *device)
{
    if (!device || device->ble_state == DEVICE_STATE_STOPPED)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ble_hid_start_advertising();
}

esp_err_t hid_device_stop_advertising(hid_device_t *device)
{
    if (!device || device->ble_state == DEVICE_STATE_STOPPED)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ble_hid_stop_advertising();
}

void hid_device_set_mouse_state(hid_device_t *device, const mouse_state_t *state)
{
    if (device && state)
    {
        device->state.mouse = *state;
        hid_device_mouse_queue_push(&device->state, state);
        hid_device_flush_reports(device, true, false, false);
    }
}

void hid_device_set_keyboard_state(hid_device_t *device, const keyboard_state_t *state)
{
    if (device && state)
    {
        device->state.keyboard = *state;
        hid_device_keyboard_queue_push(&device->state, state);
        hid_device_flush_reports(device, false, true, false);
    }
}

void hid_device_set_consumer_state(hid_device_t *device, const consumer_state_t *state)
{
    if (device && state)
    {
        device->state.consumer = *state;
        if (state->active && state->usage != 0)
        {
            uint16_t mask = ble_hid_consumer_usage_to_mask(state->usage);
            if (mask == 0)
            {
                ESP_LOGW(TAG, "Ignoring unsupported consumer usage: 0x%04X", state->usage);
                device->state.consumer_pending_release = false;
                device->state.consumer_updated = (device->state.consumer_queue.count > 0);
                return;
            }
        }
        if (state->active)
        {
            hid_device_consumer_queue_push(&device->state, state->usage);
            if (state->hold)
            {
                device->state.consumer_pending_release = true;
            }
            else
            {
                device->state.consumer_pending_release = false;
                hid_device_consumer_queue_push(&device->state, 0);
            }
        }
        else
        {
            if (device->state.consumer_pending_release)
            {
                hid_device_consumer_queue_push(&device->state, 0);
                device->state.consumer_pending_release = false;
            }
            else if (state->usage == 0)
            {
                hid_device_consumer_queue_push(&device->state, 0);
            }
        }
        device->state.consumer_updated = (device->state.consumer_queue.count > 0);
        hid_device_flush_reports(device, false, false, true);
    }
}

void hid_device_request_notify(hid_device_t *device, bool mouse, bool keyboard, bool consumer)
{
    hid_device_flush_reports(device, mouse, keyboard, consumer);
}

esp_err_t hid_device_notify_mouse(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    mouse_state_t *pending = hid_device_mouse_queue_peek(&device->state);
    if (!pending)
    {
        device->state.mouse_updated = false;
        return ESP_OK;
    }

    esp_err_t err = ble_hid_notify_mouse(pending);
    if (err == ESP_OK)
    {
        hid_device_mouse_queue_pop(&device->state);
    }

    return err;
}

esp_err_t hid_device_notify_keyboard(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    keyboard_state_t *pending = hid_device_keyboard_queue_peek(&device->state);
    if (!pending)
    {
        device->state.keyboard_updated = false;
        return ESP_OK;
    }

    esp_err_t err = ble_hid_notify_keyboard(pending);
    if (err == ESP_OK)
    {
        hid_device_keyboard_queue_pop(&device->state);
    }

    return err;
}

esp_err_t hid_device_notify_consumer(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t *usage = hid_device_consumer_queue_peek(&device->state);
    if (!usage)
    {
        device->state.consumer_updated = false;
        return ESP_OK;
    }

    esp_err_t err = ble_hid_notify_consumer(*usage);
    if (err != ESP_OK)
    {
        return err;
    }

    hid_device_consumer_queue_pop(&device->state);

    if (*usage == 0)
    {
        device->state.consumer_pending_release = false;
    }

    return ESP_OK;
}

bool hid_device_is_bonded(hid_device_t *device)
{
    if (!device)
    {
        return false;
    }
    return ble_hid_is_bonded();
}

esp_err_t hid_device_forget_peer(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return ble_hid_clear_bonds();
}

esp_err_t hid_device_update_name(hid_device_t *device, const char *name)
{
    if (!device || !name)
    {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(device->device_name, name, sizeof(device->device_name) - 1);
    ESP_LOGI(TAG, "Device name updated to: %s", device->device_name);
    return ESP_OK;
}

static void internal_state_callback(hid_device_state_t state)
{
    if (g_device)
    {
        g_device->ble_state = state;
        if (state == DEVICE_STATE_CONNECTED)
        {
            hid_device_flush_reports(g_device, true, true, true);
        }
        if (g_device->callback)
        {
            g_device->callback(state);
        }
    }
}

static void hid_device_retry_timer_callback(TimerHandle_t timer)
{
    hid_device_t *device = (hid_device_t *)pvTimerGetTimerID(timer);
    if (!device)
    {
        return;
    }

    bool mouse = false;
    bool keyboard = false;
    bool consumer = false;

    portENTER_CRITICAL(&s_retry_lock);
    mouse = s_pending_retry.mouse;
    keyboard = s_pending_retry.keyboard;
    consumer = s_pending_retry.consumer;
    s_pending_retry.mouse = false;
    s_pending_retry.keyboard = false;
    s_pending_retry.consumer = false;
    portEXIT_CRITICAL(&s_retry_lock);

    if (mouse || keyboard || consumer)
    {
        hid_device_flush_reports(device, mouse, keyboard, consumer);
    }
}

static void hid_device_schedule_retry(hid_device_t *device, bool mouse, bool keyboard, bool consumer)
{
    if (!device || device->ble_state != DEVICE_STATE_CONNECTED)
    {
        return;
    }

    portENTER_CRITICAL(&s_retry_lock);
    s_pending_retry.mouse |= mouse;
    s_pending_retry.keyboard |= keyboard;
    s_pending_retry.consumer |= consumer;
    portEXIT_CRITICAL(&s_retry_lock);

    if (!s_retry_timer)
    {
        s_retry_timer = xTimerCreateStatic("hid_flush_retry",
                                          pdMS_TO_TICKS(HID_FLUSH_RETRY_DELAY_MS),
                                          pdFALSE,
                                          device,
                                          hid_device_retry_timer_callback,
                                          &s_retry_timer_buffer);
    }
    else
    {
        vTimerSetTimerID(s_retry_timer, device);
    }

    if (!s_retry_timer)
    {
        ESP_LOGW(TAG, "Failed to create retry timer");
        return;
    }

    if (xTimerIsTimerActive(s_retry_timer))
    {
        xTimerStop(s_retry_timer, 0);
    }

    if (xTimerChangePeriod(s_retry_timer, pdMS_TO_TICKS(HID_FLUSH_RETRY_DELAY_MS), 0) != pdPASS)
    {
        ESP_LOGW(TAG, "Failed to schedule retry timer");
    }
}

static void hid_device_flush_reports(hid_device_t *device, bool mouse, bool keyboard, bool consumer)
{
    if (!device || device->ble_state != DEVICE_STATE_CONNECTED)
    {
        return;
    }

    if (mouse)
    {
        while (device->state.mouse_updated)
        {
            esp_err_t err = hid_device_notify_mouse(device);
            if (err == ESP_OK)
            {
                continue;
            }

            if (err == ESP_ERR_NO_MEM)
            {
                ESP_LOGW(TAG, "Mouse notify out of memory, scheduling retry");
                hid_device_schedule_retry(device, true, false, false);
            }
            else if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to notify mouse report: %s", esp_err_to_name(err));
            }
            break;
        }
    }

    if (keyboard)
    {
        while (device->state.keyboard_updated)
        {
            esp_err_t err = hid_device_notify_keyboard(device);
            if (err == ESP_OK)
            {
                continue;
            }

            if (err == ESP_ERR_NO_MEM)
            {
                ESP_LOGW(TAG, "Keyboard notify out of memory, scheduling retry");
                hid_device_schedule_retry(device, false, true, false);
            }
            else if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to notify keyboard report: %s", esp_err_to_name(err));
            }
            break;
        }
    }

    if (consumer)
    {
        while (device->state.consumer_updated)
        {
            esp_err_t err = hid_device_notify_consumer(device);
            if (err == ESP_ERR_NO_MEM)
            {
                ESP_LOGW(TAG, "Consumer notify out of memory, scheduling retry");
                hid_device_schedule_retry(device, false, false, true);
                break;
            }
            else if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to notify consumer report: %s", esp_err_to_name(err));
                break;
            }
        }
    }
}
