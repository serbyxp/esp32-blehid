#include "nvs_keystore.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "NVS_KEYSTORE";
static const char *NVS_NAMESPACE = "ble_bonds";

esp_err_t nvs_keystore_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

bool nvs_keystore_has_bonds(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    nvs_iterator_t it = NULL;
    err = nvs_entry_find("nvs", NVS_NAMESPACE, NVS_TYPE_ANY, &it);
    bool has_bonds = (err == ESP_OK && it != NULL);

    if (it)
    {
        nvs_release_iterator(it);
    }
    nvs_close(handle);

    return has_bonds;
}

esp_err_t nvs_keystore_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Cleared all bonding information");
    return err;
}
