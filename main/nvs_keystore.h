#ifndef NVS_KEYSTORE_H
#define NVS_KEYSTORE_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t nvs_keystore_init(void);
bool nvs_keystore_has_bonds(void);
esp_err_t nvs_keystore_clear(void);

#endif // NVS_KEYSTORE_H
