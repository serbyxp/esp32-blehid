#ifndef WIFI_MANAGER_TEST_STUBS_H
#define WIFI_MANAGER_TEST_STUBS_H

#include <stdbool.h>
#include "esp_wifi_types.h"
#include "esp_wifi.h"

void wifi_manager_test_reset_stubs(void);
int wifi_manager_test_get_scan_stop_calls(void);
int wifi_manager_test_get_connect_calls(void);
wifi_mode_t wifi_manager_test_get_wifi_mode(void);
wifi_config_t wifi_manager_test_get_sta_config(void);
int wifi_manager_test_get_scan_start_calls(void);
bool wifi_manager_test_is_sta_started(void);

#endif // WIFI_MANAGER_TEST_STUBS_H
