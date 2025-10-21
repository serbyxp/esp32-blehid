#ifndef HID_DEVICE_H
#define HID_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "hid_keyboard.h"

#define HID_MOUSE_QUEUE_DEPTH 8
#define HID_KEYBOARD_QUEUE_DEPTH 32
#define HID_CONSUMER_QUEUE_DEPTH 16

// Device states
typedef enum
{
    DEVICE_STATE_STOPPED = 0,
    DEVICE_STATE_IDLE,
    DEVICE_STATE_ADVERTISING,
    DEVICE_STATE_CONNECTED
} hid_device_state_t;

// Mouse state
typedef struct
{
    int8_t x;
    int8_t y;
    int8_t wheel;
    int8_t hwheel;
    uint8_t buttons; // bits: 0=left, 1=right, 2=middle, 3=back, 4=forward
} mouse_state_t;

typedef struct
{
    uint16_t usage;
    bool active;
    bool hold;
} consumer_state_t;

// Combined device state
typedef struct
{
    mouse_state_t mouse;
    keyboard_state_t keyboard;
    bool mouse_updated;
    bool keyboard_updated;
    consumer_state_t consumer;
    bool consumer_updated;
    bool consumer_pending_release;
    struct
    {
        mouse_state_t entries[HID_MOUSE_QUEUE_DEPTH];
        size_t head;
        size_t count;
    } mouse_queue;
    struct
    {
        keyboard_state_t entries[HID_KEYBOARD_QUEUE_DEPTH];
        size_t head;
        size_t count;
    } keyboard_queue;
    struct
    {
        uint16_t entries[HID_CONSUMER_QUEUE_DEPTH];
        size_t head;
        size_t count;
    } consumer_queue;
} device_state_t;

// Device control
typedef struct hid_device_s hid_device_t;

// Callbacks
typedef void (*state_change_callback_t)(hid_device_state_t state);

// Device management
hid_device_t *hid_device_create(const char *device_name);
void hid_device_destroy(hid_device_t *device);
esp_err_t hid_device_start(hid_device_t *device);
esp_err_t hid_device_stop(hid_device_t *device);

// State management
hid_device_state_t hid_device_get_state(hid_device_t *device);
void hid_device_set_state_callback(hid_device_t *device, state_change_callback_t callback);

// Advertising control
esp_err_t hid_device_start_advertising(hid_device_t *device);
esp_err_t hid_device_stop_advertising(hid_device_t *device);

// Input updates
void hid_device_set_mouse_state(hid_device_t *device, const mouse_state_t *state);
void hid_device_set_keyboard_state(hid_device_t *device, const keyboard_state_t *state);
void hid_device_set_consumer_state(hid_device_t *device, const consumer_state_t *state);
void hid_device_request_notify(hid_device_t *device, bool mouse, bool keyboard, bool consumer);
esp_err_t hid_device_notify_mouse(hid_device_t *device);
esp_err_t hid_device_notify_keyboard(hid_device_t *device);
esp_err_t hid_device_notify_consumer(hid_device_t *device);

// Bonding management
bool hid_device_is_bonded(hid_device_t *device);
esp_err_t hid_device_forget_peer(hid_device_t *device);

// Device name update
esp_err_t hid_device_update_name(hid_device_t *device, const char *name);

#endif // HID_DEVICE_H
