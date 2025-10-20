#include "ble_hid.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "host/ble_att.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "nvs_keystore.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

void ble_store_config_init(void);

static const char *TAG = "BLE_HID";

// Combined HID Report Descriptor (Mouse + Keyboard + Consumer)
static const uint8_t hid_report_map[] = {
    // Mouse (Report ID 1) — matches tmp_composite_example.py
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Buttons)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x95, 0x01,        //     Report Count (1) - padding
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Constant, Variable, Absolute)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0,              // End Collection

    // Keyboard (Report ID 2) — byte-for-byte match with reference
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x05,        //   Usage Maximum (5)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0,              // End Collection

    // Consumer Control (Report ID 3) — ESP32 combo bitfield layout
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x05, 0x0C,        //   Usage Page (Consumer)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x09, 0xB5,        //   Usage (Scan Next Track)
    0x09, 0xB6,        //   Usage (Scan Previous Track)
    0x09, 0xB7,        //   Usage (Stop)
    0x09, 0xCD,        //   Usage (Play/Pause)
    0x09, 0xE2,        //   Usage (Mute)
    0x09, 0xE9,        //   Usage (Volume Up)
    0x09, 0xEA,        //   Usage (Volume Down)
    0x0A, 0x23, 0x02,  //   Usage (AC Home / WWW Home)
    0x0A, 0x94, 0x01,  //   Usage (AL My Computer)
    0x0A, 0x92, 0x01,  //   Usage (AL Calculator)
    0x0A, 0x2A, 0x02,  //   Usage (AC Bookmarks)
    0x0A, 0x21, 0x02,  //   Usage (AC Search)
    0x0A, 0x26, 0x02,  //   Usage (AC Stop)
    0x0A, 0x24, 0x02,  //   Usage (AC Back)
    0x0A, 0x83, 0x01,  //   Usage (AL Consumer Control Configuration / Media Select)
    0x0A, 0x8A, 0x01,  //   Usage (AL Email Reader / Mail)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0xC0,              // End Collection
};

static const uint16_t s_consumer_usages[] = {
    0x00B5, // Scan Next Track
    0x00B6, // Scan Previous Track
    0x00B7, // Stop
    0x00CD, // Play/Pause
    0x00E2, // Mute
    0x00E9, // Volume Up
    0x00EA, // Volume Down
    0x0223, // WWW Home
    0x0194, // My Computer
    0x0192, // Calculator
    0x022A, // WWW Favorites
    0x0221, // WWW Search
    0x0226, // WWW Stop
    0x0224, // WWW Back
    0x0183, // Media Select
    0x018A, // Mail
};

// Report Reference descriptors
static const uint8_t mouse_report_ref[] = {0x01, 0x01};    // Report ID 1, Input
static const uint8_t keyboard_input_ref[] = {0x02, 0x01};  // Report ID 2, Input
static const uint8_t keyboard_output_ref[] = {0x02, 0x02}; // Report ID 2, Output
static const uint8_t consumer_report_ref[] = {0x03, 0x01}; // Report ID 3, Input

// UUIDs
#define HID_SERVICE_UUID 0x1812
#define DEVICE_INFO_SERVICE_UUID 0x180A
#define BATTERY_SERVICE_UUID 0x180F
#define HID_INFO_UUID 0x2A4A
#define HID_REPORT_MAP_UUID 0x2A4B
#define HID_CONTROL_POINT_UUID 0x2A4C
#define HID_REPORT_UUID 0x2A4D
#define HID_PROTOCOL_MODE_UUID 0x2A4E
#define BOOT_KEYBOARD_INPUT_UUID 0x2A22
#define BOOT_KEYBOARD_OUTPUT_UUID 0x2A32
#define BOOT_MOUSE_INPUT_UUID 0x2A33
#define PNP_ID_UUID 0x2A50
#define BATTERY_LEVEL_UUID 0x2A19
#define REPORT_REFERENCE_UUID 0x2908

// State
static ble_hid_handles_t s_handles = {0};
static void (*s_state_callback)(hid_device_state_t) = NULL;
static ble_connection_info_t s_conn_info = {0};
static char s_device_name[32] = "ESP32 HID";
static uint8_t s_adv_handle = 0;

// Report storage
static uint8_t s_mouse_report[5] = {0};
static uint8_t s_keyboard_report[9] = {0};
static uint16_t s_consumer_report = 0;
static uint8_t s_boot_mouse_report[3] = {0};
static uint8_t s_boot_keyboard_report[8] = {0};
static uint8_t s_keyboard_leds = 0;
static uint8_t s_battery_level = 100;
static uint8_t s_protocol_mode = 1; // Report protocol
static uint8_t s_hid_control = 0;

// Forward declarations
static int ble_hid_gap_event(struct ble_gap_event *event, void *arg);
static void ble_hid_on_sync(void);
static void ble_hid_on_reset(int reason);

// HID Information
static int hid_info_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    static const uint8_t hid_info[] = {
        0x11, 0x01, // bcdHID 1.11
        0x00,       // bCountryCode
        0x03        // Flags: RemoteWake | NormallyConnectable
    };

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// HID Report Map
static int hid_report_map_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// HID Control Point
static int hid_control_point_access(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0)
        {
            os_mbuf_copydata(ctxt->om, 0, 1, &s_hid_control);
            ESP_LOGI(TAG, "HID Control Point: 0x%02x", s_hid_control);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Protocol Mode
static int hid_protocol_mode_access(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, &s_protocol_mode, 1);
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0)
        {
            os_mbuf_copydata(ctxt->om, 0, 1, &s_protocol_mode);
            ESP_LOGI(TAG, "Protocol Mode: %d", s_protocol_mode);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Mouse Report (Report ID 1)
static int mouse_report_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, s_mouse_report, sizeof(s_mouse_report));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Keyboard Input Report (Report ID 2)
static int keyboard_input_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, s_keyboard_report, sizeof(s_keyboard_report));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Keyboard Output Report (Report ID 2 - LEDs)
static int keyboard_output_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, &s_keyboard_leds, 1);
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0)
        {
            os_mbuf_copydata(ctxt->om, 0, 1, &s_keyboard_leds);
            ESP_LOGI(TAG, "Keyboard LEDs: 0x%02x", s_keyboard_leds);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Consumer Report (Report ID 3)
uint16_t ble_hid_consumer_usage_to_mask(uint16_t usage)
{
    if (usage == 0)
    {
        return 0;
    }

    size_t count = sizeof(s_consumer_usages) / sizeof(s_consumer_usages[0]);
    for (size_t i = 0; i < count; ++i)
    {
        uint16_t mask = (uint16_t)(1u << i);
        if (usage == s_consumer_usages[i] || usage == mask)
        {
            return mask;
        }
    }

    return 0;
}

static int consumer_report_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        // Keep the readback order consistent with the notification payload
        uint8_t report[2] = {
            (uint8_t)(s_consumer_report & 0xFF),
            (uint8_t)((s_consumer_report >> 8) & 0xFF)
        };
        return os_mbuf_append(ctxt->om, report, sizeof(report));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Boot Mouse Input
static int boot_mouse_input_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, s_boot_mouse_report, sizeof(s_boot_mouse_report));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Boot Keyboard Input
static int boot_keyboard_input_access(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, s_boot_keyboard_report, sizeof(s_boot_keyboard_report));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Boot Keyboard Output
static int boot_keyboard_output_access(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, &s_keyboard_leds, 1);
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0)
        {
            os_mbuf_copydata(ctxt->om, 0, 1, &s_keyboard_leds);
            ESP_LOGI(TAG, "Boot Keyboard LEDs: 0x%02x", s_keyboard_leds);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Report Reference Descriptor
static int report_reference_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC)
    {
        const uint8_t *ref = (const uint8_t *)arg;
        return os_mbuf_append(ctxt->om, ref, 2);
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// PnP ID (required for iOS)
static int pnp_id_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    static const uint8_t pnp_id[] = {
        0x02,       // Vendor ID Source: USB Implementer's Forum
        0x5E, 0x04, // Vendor ID: 0x045E (Microsoft, for compatibility)
        0x00, 0x00, // Product ID: 0x0000
        0x00, 0x01  // Product Version: 1.0
    };

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, pnp_id, sizeof(pnp_id));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Battery Level
static int battery_level_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(ctxt->om, &s_battery_level, 1);
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// GATT service definitions - SINGLE HID SERVICE with multiple reports
static const struct ble_gatt_svc_def gatt_svcs[] = {
    // Device Information Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(DEVICE_INFO_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(PNP_ID_UUID),
                .access_cb = pnp_id_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {0}}},

    // Battery Service
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(BATTERY_SERVICE_UUID), .characteristics = (struct ble_gatt_chr_def[]){{
                                                                                                                                             .uuid = BLE_UUID16_DECLARE(BATTERY_LEVEL_UUID),
                                                                                                                                             .access_cb = battery_level_access,
                                                                                                                                             .val_handle = &s_handles.battery_level_handle,
                                                                                                                                             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                                                                                                                         },
                                                                                                                                         {0}}},

    // HID Service (SINGLE service with all reports)
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(HID_SERVICE_UUID), .characteristics = (struct ble_gatt_chr_def[]){// HID Information
                                                                                                                                     {
                                                                                                                                         .uuid = BLE_UUID16_DECLARE(HID_INFO_UUID),
                                                                                                                                         .access_cb = hid_info_access,
                                                                                                                                         .flags = BLE_GATT_CHR_F_READ,
                                                                                                                                     },
      // Report Map

      {
          .uuid = BLE_UUID16_DECLARE(HID_REPORT_MAP_UUID),
          .access_cb = hid_report_map_access,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
          .min_key_size = 16,
      },
      // HID Control Point

      {
          .uuid = BLE_UUID16_DECLARE(HID_CONTROL_POINT_UUID),
          .access_cb = hid_control_point_access,
          .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
          .min_key_size = 16,
      },
      // Protocol Mode

      {
          .uuid = BLE_UUID16_DECLARE(HID_PROTOCOL_MODE_UUID),
          .access_cb = hid_protocol_mode_access,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
          .min_key_size = 16,
      },

                                                                                                                                     // Mouse Report (ID 1)
                                                                                                                                     {.uuid = BLE_UUID16_DECLARE(HID_REPORT_UUID), .access_cb = mouse_report_access, .val_handle = &s_handles.mouse_report_handle, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC, .min_key_size = 16, .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                                                                                                                                                                                                                                                                                                                                                                      .uuid = BLE_UUID16_DECLARE(REPORT_REFERENCE_UUID),
                                                                                                                                                                                                                                                                                                                                                                                                                      .att_flags = BLE_ATT_F_READ,
                                                                                                                                                                                                                                                                                                                                                                                                                      .access_cb = report_reference_access,
                                                                                                                                                                                                                                                                                                                                                                                                                      .arg = (void *)mouse_report_ref,
                                                                                                                                                                                                                                                                                                                                                                                                                  },
                                                                                                                                                                                                                                                                                                                                                                                                                  {0}}},

                                                                                                                                     // Keyboard Input Report (ID 2)
                                                                                                                                     {.uuid = BLE_UUID16_DECLARE(HID_REPORT_UUID), .access_cb = keyboard_input_access, .val_handle = &s_handles.keyboard_input_handle, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC, .min_key_size = 16, .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                                                                                                                                                                                                                                                                                                                                                                          .uuid = BLE_UUID16_DECLARE(REPORT_REFERENCE_UUID),
                                                                                                                                                                                                                                                                                                                                                                                                                          .att_flags = BLE_ATT_F_READ,
                                                                                                                                                                                                                                                                                                                                                                                                                          .access_cb = report_reference_access,
                                                                                                                                                                                                                                                                                                                                                                                                                          .arg = (void *)keyboard_input_ref,
                                                                                                                                                                                                                                                                                                                                                                                                                      },
                                                                                                                                                                                                                                                                                                                                                                                                                      {0}}},

                                                                                                                                     // Keyboard Output Report (ID 2 - LEDs)
                                                                                                                                     {.uuid = BLE_UUID16_DECLARE(HID_REPORT_UUID), .access_cb = keyboard_output_access, .val_handle = &s_handles.keyboard_output_handle, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC, .min_key_size = 16, .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    .uuid = BLE_UUID16_DECLARE(REPORT_REFERENCE_UUID),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    .att_flags = BLE_ATT_F_READ,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    .access_cb = report_reference_access,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    .arg = (void *)keyboard_output_ref,
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                },
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                {0}}},

                                                                                                                                     // Consumer Report (ID 3)
                                                                                                                                     {.uuid = BLE_UUID16_DECLARE(HID_REPORT_UUID), .access_cb = consumer_report_access, .val_handle = &s_handles.consumer_input_handle, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC, .min_key_size = 16, .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                                                                                                                                                                                                                                                                                                                                                                           .uuid = BLE_UUID16_DECLARE(REPORT_REFERENCE_UUID),
                                                                                                                                                                                                                                                                                                                                                                                                                           .att_flags = BLE_ATT_F_READ,
                                                                                                                                                                                                                                                                                                                                                                                                                           .access_cb = report_reference_access,
                                                                                                                                                                                                                                                                                                                                                                                                                           .arg = (void *)consumer_report_ref,
                                                                                                                                                                                                                                                                                                                                                                                                                       },
                                                                                                                                                                                                                                                                                                                                                                                                                       {0}}},

                                                                                                                                     // Boot Mouse Input
                                                                                                                                     {
                                                                                                                                         .uuid = BLE_UUID16_DECLARE(BOOT_MOUSE_INPUT_UUID),
                                                                                                                                         .access_cb = boot_mouse_input_access,
                                                                                                                                         .val_handle = &s_handles.mouse_boot_input_handle,
                                                                                                                                         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                                                                                                                                         .min_key_size = 16,
                                                                                                                                     },

                                                                                                                                     // Boot Keyboard Input
                                                                                                                                     {
                                                                                                                                         .uuid = BLE_UUID16_DECLARE(BOOT_KEYBOARD_INPUT_UUID),
                                                                                                                                         .access_cb = boot_keyboard_input_access,
                                                                                                                                         .val_handle = &s_handles.keyboard_boot_input_handle,
                                                                                                                                         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                                                                                                                                         .min_key_size = 16,
                                                                                                                                     },

                                                                                                                                     // Boot Keyboard Output
                                                                                                                                     {
                                                                                                                                         .uuid = BLE_UUID16_DECLARE(BOOT_KEYBOARD_OUTPUT_UUID),
                                                                                                                                         .access_cb = boot_keyboard_output_access,
                                                                                                                                         .val_handle = &s_handles.keyboard_boot_output_handle,
                                                                                                                                         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
                                                                                                                                         .min_key_size = 16,
                                                                                                                                     },

                                                                                                                                     {0}}},
    {0}};

static int ble_hid_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0)
        {
            s_handles.conn_handle = event->connect.conn_handle;
            s_handles.connected = true;

            memset(&s_conn_info, 0, sizeof(s_conn_info));
            s_conn_info.connected = true;

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0)
            {
                const ble_addr_t *addr = &desc.peer_id_addr;
                bool addr_valid = false;
                for (int i = 0; i < 6; ++i)
                {
                    if (addr->val[i] != 0)
                    {
                        addr_valid = true;
                        break;
                    }
                }
                if (!addr_valid)
                {
                    addr = &desc.peer_ota_addr;
                }
                memcpy(s_conn_info.peer_addr, addr->val, sizeof(s_conn_info.peer_addr));
                s_conn_info.peer_addr_type = addr->type;
                s_conn_info.bonded = desc.sec_state.bonded;
                s_conn_info.encrypted = desc.sec_state.encrypted;
                s_conn_info.authenticated = desc.sec_state.authenticated;
            }

            if (s_state_callback)
            {
                s_state_callback(DEVICE_STATE_CONNECTED);
            }

            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY && rc != BLE_HS_ENOTSUP)
            {
                ESP_LOGW(TAG, "Failed to initiate security (rc=%d)", rc);
            }
        }
        else
        {
            if (s_state_callback)
            {
                s_state_callback(DEVICE_STATE_IDLE);
            }
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
        s_handles.connected = false;
        s_handles.subscribed_mouse = false;
        s_handles.subscribed_mouse_boot = false;
        s_handles.subscribed_keyboard = false;
        s_handles.subscribed_keyboard_boot = false;
        s_handles.subscribed_consumer = false;
        memset(&s_conn_info, 0, sizeof(s_conn_info));

        if (s_state_callback)
        {
            s_state_callback(DEVICE_STATE_IDLE);
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete");
        if (!s_handles.connected && s_state_callback)
        {
            s_state_callback(DEVICE_STATE_IDLE);
        }
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; attr_handle=%d, subscribed=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);

        if (event->subscribe.attr_handle == s_handles.mouse_report_handle)
        {
            s_handles.subscribed_mouse = event->subscribe.cur_notify;
        }
        else if (event->subscribe.attr_handle == s_handles.keyboard_input_handle)
        {
            s_handles.subscribed_keyboard = event->subscribe.cur_notify;
        }
        else if (event->subscribe.attr_handle == s_handles.mouse_boot_input_handle)
        {
            s_handles.subscribed_mouse_boot = event->subscribe.cur_notify;
        }
        else if (event->subscribe.attr_handle == s_handles.keyboard_boot_input_handle)
        {
            s_handles.subscribed_keyboard_boot = event->subscribe.cur_notify;
        }
        else if (event->subscribe.attr_handle == s_handles.consumer_input_handle)
        {
            s_handles.subscribed_consumer = event->subscribe.cur_notify;
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
    {
        struct ble_sm_io pkey = {0};

        ESP_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);

        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // Static passkey for testing
            ESP_LOGI(TAG, "===========================================");
            ESP_LOGI(TAG, "ENTER PASSKEY ON YOUR DEVICE: %06lu", (unsigned long)pkey.passkey);
            ESP_LOGI(TAG, "===========================================");
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP)
        {
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            ESP_LOGI(TAG, "Numeric comparison: %06lu (auto-accepting)",
                     (unsigned long)event->passkey.params.numcmp);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_INPUT)
        {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456;
            ESP_LOGI(TAG, "Passkey input requested, using: %06lu", (unsigned long)pkey.passkey);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_NONE)
        {
            ESP_LOGI(TAG, "No passkey action required");
            return 0;
        }
        else
        {
            ESP_LOGW(TAG, "Unhandled passkey action: %d", event->passkey.params.action);
            return 0;
        }

        int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "Failed to inject passkey; rc=%d", rc);
        }
        break;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change; status=%d", event->enc_change.status);
        if (event->enc_change.status == 0)
        {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
            {
                ESP_LOGI(TAG, "Security: bonded=%d encrypted=%d authenticated=%d key_size=%d",
                         desc.sec_state.bonded,
                         desc.sec_state.encrypted,
                         desc.sec_state.authenticated,
                         desc.sec_state.key_size);
                s_conn_info.bonded = desc.sec_state.bonded;
                s_conn_info.encrypted = desc.sec_state.encrypted;
                s_conn_info.authenticated = desc.sec_state.authenticated;
                if (s_state_callback && s_handles.connected)
                {
                    s_state_callback(DEVICE_STATE_CONNECTED);
                }
            }
        }
        break;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGD(TAG, "Notify TX; status=%d", event->notify_tx.status);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGI(TAG, "Repeat pairing event; deleting old bond");
        {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
            {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        break;
    }

    return 0;
}

static void ble_hid_on_sync(void)
{
    int rc;

    ESP_LOGI(TAG, "BLE stack synchronized");

    // Ensure we have a random address
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set address; rc=%d", rc);
        return;
    }

    // Figure out address to use
    rc = ble_hs_id_infer_auto(0, &s_adv_handle);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to infer address; rc=%d", rc);
        return;
    }

    uint8_t addr[6];
    rc = ble_hs_id_copy_addr(s_adv_handle, addr, NULL);
    if (rc == 0)
    {
        ESP_LOGI(TAG, "Device Address: %02x:%02x:%02x:%02x:%02x:%02x",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }
}

static void ble_hid_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE reset; reason=%d", reason);
}

static void ble_hid_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_hid_init(const char *device_name)
{
    if (device_name)
    {
        strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(nimble_port_init());

    // Configure BLE host
    ble_hs_cfg.sync_cb = ble_hid_on_sync;
    ble_hs_cfg.reset_cb = ble_hid_on_reset;

    // Security configuration for bonding
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1; // Secure Connections
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // iOS compatibility: use "Display Only" for better pairing experience
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;

    // Initialize services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Initialize bonding storage
    ble_store_config_init();
    nvs_keystore_init();

    // Register GATT services
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to count GATT config; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to add GATT services; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_start();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start GATT services; rc=%d", rc);
        return ESP_FAIL;
    }

    // Set device name and appearance
    ble_svc_gap_device_name_set(s_device_name);
    ble_svc_gap_device_appearance_set(0x03C2); // Generic HID appearance

    // Start the BLE host task
    nimble_port_freertos_init(ble_hid_host_task);

    ESP_LOGI(TAG, "BLE HID initialized successfully");
    return ESP_OK;
}

esp_err_t ble_hid_deinit(void)
{
    int rc = nimble_port_stop();
    if (rc == 0)
    {
        nimble_port_deinit();
    }
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_hid_start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};

    // Advertising data
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = 0x03C2; // Generic HID
    fields.appearance_is_present = 1;

    // Include HID service UUID
    ble_uuid16_t uuids16[] = {
        BLE_UUID16_INIT(HID_SERVICE_UUID)};
    fields.uuids16 = uuids16;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set adv fields; rc=%d", rc);
        return ESP_FAIL;
    }

    // Scan response data
    rsp_fields.name = (uint8_t *)s_device_name;
    rsp_fields.name_len = strlen(s_device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set scan response fields; rc=%d", rc);
        return ESP_FAIL;
    }

    // Advertising parameters
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

    rc = ble_gap_adv_start(s_adv_handle, BLE_ADDR_PUBLIC, BLE_HS_FOREVER,
                           &adv_params, ble_hid_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start advertising; rc=%d", rc);
        return ESP_FAIL;
    }

    if (s_state_callback)
    {
        s_state_callback(DEVICE_STATE_ADVERTISING);
    }

    ESP_LOGI(TAG, "Advertising started");
    return ESP_OK;
}

esp_err_t ble_hid_stop_advertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY)
    {
        ESP_LOGE(TAG, "Failed to stop advertising; rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Advertising stopped");

    if (s_state_callback)
    {
        s_state_callback(DEVICE_STATE_IDLE);
    }

    return ESP_OK;
}

esp_err_t ble_hid_notify_mouse(const mouse_state_t *state)
{
    if (!s_handles.connected)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_handles.subscribed_mouse && !s_handles.subscribed_mouse_boot)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Update report storage
    s_mouse_report[0] = 0x01;           // Report ID
    s_mouse_report[1] = state->buttons & 0x07;
    s_mouse_report[2] = (int8_t)state->x;  // Cast to ensure sign extension
    s_mouse_report[3] = (int8_t)state->y;
    s_mouse_report[4] = (int8_t)state->wheel;

    // Boot report (no Report ID)
    s_boot_mouse_report[0] = state->buttons & 0x07;
    s_boot_mouse_report[1] = (int8_t)state->x;
    s_boot_mouse_report[2] = (int8_t)state->y;

    esp_err_t result = ESP_OK;

    if (s_handles.subscribed_mouse)
    {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s_mouse_report, sizeof(s_mouse_report));
        if (!om)
        {
            return ESP_ERR_NO_MEM;
        }

        int rc = ble_gatts_notify_custom(s_handles.conn_handle,
                                         s_handles.mouse_report_handle, om);
        if (rc != 0)
        {
            ESP_LOGW(TAG, "Mouse notify failed; rc=%d", rc);
            result = ESP_FAIL;
        }
    }

    if (s_handles.subscribed_mouse_boot)
    {
        struct os_mbuf *om_boot = ble_hs_mbuf_from_flat(s_boot_mouse_report,
                                                        sizeof(s_boot_mouse_report));
        if (!om_boot)
        {
            return ESP_ERR_NO_MEM;
        }

        int rc = ble_gatts_notify_custom(s_handles.conn_handle,
                                         s_handles.mouse_boot_input_handle, om_boot);
        if (rc != 0)
        {
            ESP_LOGW(TAG, "Boot mouse notify failed; rc=%d", rc);
            result = ESP_FAIL;
        }
    }

    return result;
}

esp_err_t ble_hid_notify_keyboard(const keyboard_state_t *state)
{
    if (!s_handles.connected)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_handles.subscribed_keyboard && !s_handles.subscribed_keyboard_boot)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Update report storage
    s_keyboard_report[0] = 0x02; // Report ID
    s_keyboard_report[1] = state->modifiers;
    s_keyboard_report[2] = state->reserved;
    memcpy(&s_keyboard_report[3], state->keys, 6);

    // Boot report (no Report ID)
    s_boot_keyboard_report[0] = state->modifiers;
    s_boot_keyboard_report[1] = state->reserved;
    memcpy(&s_boot_keyboard_report[2], state->keys, 6);

    esp_err_t result = ESP_OK;

    if (s_handles.subscribed_keyboard)
    {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s_keyboard_report,
                                                   sizeof(s_keyboard_report));
        if (!om)
        {
            return ESP_ERR_NO_MEM;
        }

        int rc = ble_gatts_notify_custom(s_handles.conn_handle,
                                         s_handles.keyboard_input_handle, om);
        if (rc != 0)
        {
            ESP_LOGW(TAG, "Keyboard notify failed; rc=%d", rc);
            result = ESP_FAIL;
        }
    }

    if (s_handles.subscribed_keyboard_boot)
    {
        struct os_mbuf *om_boot = ble_hs_mbuf_from_flat(s_boot_keyboard_report,
                                                        sizeof(s_boot_keyboard_report));
        if (!om_boot)
        {
            return ESP_ERR_NO_MEM;
        }

        int rc = ble_gatts_notify_custom(s_handles.conn_handle,
                                         s_handles.keyboard_boot_input_handle, om_boot);
        if (rc != 0)
        {
            ESP_LOGW(TAG, "Boot keyboard notify failed; rc=%d", rc);
            result = ESP_FAIL;
        }
    }

    return result;
}

esp_err_t ble_hid_notify_consumer(uint16_t usage_mask)
{
    if (!s_handles.connected || !s_handles.subscribed_consumer)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t report_mask = ble_hid_consumer_usage_to_mask(usage_mask);

    if (usage_mask != 0 && report_mask == 0)
    {
        ESP_LOGW(TAG, "Unsupported consumer usage: 0x%04X", usage_mask);
    }

    s_consumer_report = report_mask;

    uint8_t report[3];
    report[0] = 0x03; // Report ID
    report[1] = report_mask & 0xFF;
    report[2] = (report_mask >> 8) & 0xFF;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (!om)
    {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_handles.conn_handle,
                                     s_handles.consumer_input_handle, om);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Consumer notify failed; rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool ble_hid_is_connected(void)
{
    return s_handles.connected;
}

uint16_t ble_hid_get_conn_handle(void)
{
    return s_handles.conn_handle;
}

void ble_hid_set_state_callback(void (*callback)(hid_device_state_t state))
{
    s_state_callback = callback;
}

bool ble_hid_is_bonded(void)
{
    return nvs_keystore_has_bonds();
}

esp_err_t ble_hid_clear_bonds(void)
{
    esp_err_t err = nvs_keystore_clear();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Bonds cleared successfully");
    }
    return err;
}

bool ble_hid_get_connection_info(ble_connection_info_t *info)
{
    if (!info)
    {
        return false;
    }

    *info = s_conn_info;
    return s_conn_info.connected;
}
