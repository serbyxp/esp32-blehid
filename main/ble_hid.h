#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "host/ble_gap.h"
#include "hid_device.h"

// BLE HID service handles
typedef struct
{
    // Report handles
    uint16_t mouse_report_handle;
    uint16_t mouse_boot_input_handle;
    uint16_t keyboard_input_handle;
    uint16_t keyboard_boot_input_handle;
    uint16_t keyboard_boot_output_handle;
    uint16_t keyboard_output_handle;
    uint16_t consumer_input_handle;
    uint16_t battery_level_handle;

    // Connection state
    uint16_t conn_handle;
    bool connected;

    // Subscription state
    bool subscribed_mouse;
    bool subscribed_mouse_boot;
    bool subscribed_keyboard;
    bool subscribed_keyboard_boot;
    bool subscribed_consumer;
} ble_hid_handles_t;

typedef struct
{
    bool connected;
    bool bonded;
    bool encrypted;
    bool authenticated;
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;
} ble_connection_info_t;

// Initialize BLE HID stack
esp_err_t ble_hid_init(const char *device_name);
esp_err_t ble_hid_deinit(void);

// Advertising control
esp_err_t ble_hid_start_advertising(void);
esp_err_t ble_hid_stop_advertising(void);

// Send HID reports
esp_err_t ble_hid_notify_mouse(const mouse_state_t *state);
esp_err_t ble_hid_notify_keyboard(const keyboard_state_t *state);
esp_err_t ble_hid_notify_consumer(uint16_t usage);

// Connection state
bool ble_hid_is_connected(void);
uint16_t ble_hid_get_conn_handle(void);
bool ble_hid_get_connection_info(ble_connection_info_t *info);

// State change callback
void ble_hid_set_state_callback(void (*callback)(hid_device_state_t state));

// Bonding management
bool ble_hid_is_bonded(void);
esp_err_t ble_hid_clear_bonds(void);

#endif // BLE_HID_H
