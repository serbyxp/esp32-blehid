#ifndef ESP_MAC_H
#define ESP_MAC_H

#include <stdint.h>

#define ESP_MAC_WIFI_STA 0

static inline int esp_read_mac(uint8_t *mac, int type)
{
    (void)type;
    for (int i = 0; i < 6; ++i)
    {
        mac[i] = (uint8_t)(0xA0 + i);
    }
    return 0;
}

#endif // ESP_MAC_H
