#include "hid_device.h"
#include "ble_hid.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HID_DEVICE";

struct hid_device_s
{
    char device_name[32];
    device_state_t state;
    hid_device_state_t ble_state;
    state_change_callback_t callback;
};

static void internal_state_callback(hid_device_state_t state);
static hid_device_t *g_device = NULL;

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
        device->state.mouse_updated = true;
    }
}

void hid_device_set_keyboard_state(hid_device_t *device, const keyboard_state_t *state)
{
    if (device && state)
    {
        device->state.keyboard = *state;
        device->state.keyboard_updated = true;
    }
}

void hid_device_set_consumer_state(hid_device_t *device, const consumer_state_t *state)
{
    if (device && state)
    {
        device->state.consumer = *state;
        device->state.consumer_updated = true;
        if (!state->active)
        {
            device->state.consumer_pending_release = false;
        }
    }
}

esp_err_t hid_device_notify_mouse(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!device->state.mouse_updated)
    {
        return ESP_OK;
    }

    esp_err_t err = ble_hid_notify_mouse(&device->state.mouse);
    if (err == ESP_OK)
    {
        device->state.mouse_updated = false;
    }

    return err;
}

esp_err_t hid_device_notify_keyboard(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!device->state.keyboard_updated)
    {
        return ESP_OK;
    }

    esp_err_t err = ble_hid_notify_keyboard(&device->state.keyboard);
    if (err == ESP_OK)
    {
        device->state.keyboard_updated = false;
    }

    return err;
}

esp_err_t hid_device_notify_consumer(hid_device_t *device)
{
    if (!device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!device->state.consumer_pending_release && !device->state.consumer_updated)
    {
        return ESP_OK;
    }

    bool sending_release = device->state.consumer_pending_release;
    uint16_t usage = 0;

    if (sending_release)
    {
        usage = 0;
    }
    else
    {
        usage = device->state.consumer.active ? device->state.consumer.usage : 0;
    }

    esp_err_t err = ble_hid_notify_consumer(usage);
    if (err != ESP_OK)
    {
        return err;
    }

    if (sending_release)
    {
        device->state.consumer_pending_release = false;
        // Preserve consumer_updated in case a new event arrived while we were releasing.
    }
    else
    {
        if (device->state.consumer.active && !device->state.consumer.hold)
        {
            device->state.consumer_pending_release = true;
            device->state.consumer.active = false;
        }
        device->state.consumer_updated = false;
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
        if (g_device->callback)
        {
            g_device->callback(state);
        }
    }
}
