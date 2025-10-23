#ifndef ESP_NETIF_H
#define ESP_NETIF_H

#include "esp_err.h"
#include "lwip/inet.h"
#include <stdint.h>

typedef struct esp_netif
{
    int placeholder;
} esp_netif_t;

typedef struct
{
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4_addr_t gw;
} esp_netif_ip_info_t;

esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *hostname);
const char *esp_netif_get_ifkey(esp_netif_t *netif);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip_info);
esp_err_t esp_netif_init(void);

#endif // ESP_NETIF_H
