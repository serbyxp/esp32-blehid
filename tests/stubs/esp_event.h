#ifndef ESP_EVENT_H
#define ESP_EVENT_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stddef.h>
#include <stdint.h>

typedef const char *esp_event_base_t;

#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t name = #name

#define WIFI_EVENT "WIFI_EVENT"
#define IP_EVENT "IP_EVENT"
#define ESP_EVENT_ANY_ID (-1)

typedef struct
{
    void *esp_netif;
    esp_netif_ip_info_t ip_info;
} ip_event_got_ip_t;

enum
{
    IP_EVENT_STA_GOT_IP = 0,
};

esp_err_t esp_event_post(esp_event_base_t base, int32_t event_id, void *event_data, size_t event_data_size, uint32_t ticks_to_wait);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id,
                                     void (*event_handler)(void *, esp_event_base_t, int32_t, void *),
                                     void *event_handler_arg);

#endif // ESP_EVENT_H
