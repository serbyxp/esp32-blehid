#ifndef MDNS_H
#define MDNS_H

#include "esp_err.h"

static inline esp_err_t mdns_register_netif(void *netif)
{
    (void)netif;
    return ESP_OK;
}

static inline esp_err_t mdns_service_txt_item_set(const char *service, const char *proto, const char *key, const char *value)
{
    (void)service;
    (void)proto;
    (void)key;
    (void)value;
    return ESP_OK;
}

static inline esp_err_t mdns_init(void)
{
    return ESP_OK;
}

static inline esp_err_t mdns_hostname_set(const char *hostname)
{
    (void)hostname;
    return ESP_OK;
}

static inline esp_err_t mdns_instance_name_set(const char *instance)
{
    (void)instance;
    return ESP_OK;
}

static inline esp_err_t mdns_service_add(const char *instance, const char *service, const char *proto, uint16_t port, void *txt, int num_items)
{
    (void)instance;
    (void)service;
    (void)proto;
    (void)port;
    (void)txt;
    (void)num_items;
    return ESP_OK;
}

#endif // MDNS_H
