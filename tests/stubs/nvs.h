#ifndef NVS_H
#define NVS_H

#include "esp_err.h"
#include <stddef.h>

typedef void *nvs_handle_t;

typedef enum
{
    NVS_READONLY = 0,
    NVS_READWRITE = 1,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_close(nvs_handle_t handle);
esp_err_t nvs_erase_all(nvs_handle_t handle);

#endif // NVS_H
