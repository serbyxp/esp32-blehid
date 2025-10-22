#include "http_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "dns_server.h"
#include "wifi_manager.h"
#include "transport_ws.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <stdbool.h>

static const char *TAG = "HTTP_SERVER";

static httpd_handle_t s_server = NULL;
static bool s_captive_portal_enabled = false;
extern const char html_start[] asm("_binary_index_html_start");
extern const char html_end[] asm("_binary_index_html_end");

static void populate_wifi_status_json(cJSON *obj)
{
    if (!obj)
    {
        return;
    }

    wifi_mode_t mode = wifi_manager_get_mode();
    const char *mode_str = "none";

    if (mode == WIFI_MODE_STA)
    {
        mode_str = wifi_manager_is_connected() ? "sta" : "connecting";
    }
    else if (mode == WIFI_MODE_AP)
    {
        mode_str = "ap";
    }
    else if (mode == WIFI_MODE_APSTA)
    {
        mode_str = wifi_manager_is_connected() ? "apsta" : "apsta_connecting";
    }

    cJSON_AddStringToObject(obj, "mode", mode_str);
    cJSON_AddBoolToObject(obj, "connected", wifi_manager_is_connected());
    cJSON_AddBoolToObject(obj, "scanning", wifi_manager_is_scanning());
    cJSON_AddNumberToObject(obj, "retry_count", wifi_manager_get_retry_count());
    cJSON_AddStringToObject(obj, "hostname", wifi_manager_get_hostname());
    cJSON_AddStringToObject(obj, "ap_ssid", wifi_manager_get_ap_ssid());

    char ip[16] = {0};
    if (wifi_manager_get_ip(ip, sizeof(ip)) == ESP_OK)
    {
        cJSON_AddStringToObject(obj, "ip", ip);
    }

    wifi_manager_sta_info_t sta_info;
    if (wifi_manager_get_sta_info(&sta_info) == ESP_OK)
    {
        cJSON *sta = cJSON_CreateObject();
        if (sta)
        {
            cJSON_AddBoolToObject(sta, "connected", sta_info.connected);
            cJSON_AddStringToObject(sta, "ssid", sta_info.ssid);
            cJSON_AddStringToObject(sta, "ip", sta_info.ip);
            cJSON_AddNumberToObject(sta, "rssi", sta_info.rssi);
            cJSON_AddNumberToObject(sta, "retry_count", sta_info.retry_count);
            cJSON_AddItemToObject(obj, "sta", sta);
        }
    }

    char mac_buffer[18];
    if (wifi_manager_get_mac_str(WIFI_IF_AP, mac_buffer, sizeof(mac_buffer)) == ESP_OK)
    {
        cJSON_AddStringToObject(obj, "mac_ap", mac_buffer);
    }
    if (wifi_manager_get_mac_str(WIFI_IF_STA, mac_buffer, sizeof(mac_buffer)) == ESP_OK)
    {
        cJSON_AddStringToObject(obj, "mac_sta", mac_buffer);
    }
}

void http_server_publish_wifi_status(void)
{
    cJSON *msg = cJSON_CreateObject();
    if (!msg)
    {
        return;
    }

    cJSON_AddStringToObject(msg, "type", "wifi_status");
    populate_wifi_status_json(msg);

    char *payload = cJSON_PrintUnformatted(msg);
    if (payload)
    {
        esp_err_t err = transport_websocket_send(payload);
        if (err != ESP_OK)
        {
            ESP_LOGD(TAG, "Failed to broadcast wifi_status: %s", esp_err_to_name(err));
        }
        free(payload);
    }
    cJSON_Delete(msg);
}

void http_server_publish_scan_results(const wifi_ap_record_t *records, uint16_t count)
{
    cJSON *msg = cJSON_CreateObject();
    if (!msg)
    {
        return;
    }

    cJSON_AddStringToObject(msg, "type", "scan_results");
    cJSON *array = cJSON_CreateArray();
    if (!array)
    {
        cJSON_Delete(msg);
        return;
    }
    cJSON_AddItemToObject(msg, "networks", array);

    for (uint16_t i = 0; records && i < count; ++i)
    {
        cJSON *entry = cJSON_CreateObject();
        if (!entry)
        {
            continue;
        }

        char ssid[33] = {0};
        memcpy(ssid, records[i].ssid, sizeof(records[i].ssid));
        cJSON_AddStringToObject(entry, "ssid", ssid);
        cJSON_AddNumberToObject(entry, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(entry, "channel", records[i].primary);
        cJSON_AddNumberToObject(entry, "auth", records[i].authmode);

        cJSON_AddItemToArray(array, entry);
    }

    char *payload = cJSON_PrintUnformatted(msg);
    if (payload)
    {
        esp_err_t err = transport_websocket_send(payload);
        if (err != ESP_OK)
        {
            ESP_LOGD(TAG, "Failed to broadcast scan_results: %s", esp_err_to_name(err));
        }
        free(payload);
    }

    cJSON_Delete(msg);
}

// Root handler - serves HTML
static esp_err_t root_handler(httpd_req_t *req)
{
    const size_t html_size = (html_end - html_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_start, html_size);
    return ESP_OK;
}

// API handler for WiFi operations
static esp_err_t api_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(buf) - 1)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *req_json = cJSON_Parse(buf);
    if (!req_json)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON *cmd_item = cJSON_GetObjectItem(req_json, "cmd");

    if (cmd_item && cJSON_IsString(cmd_item))
    {
        const char *cmd = cmd_item->valuestring;

        if (strcmp(cmd, "get_status") == 0)
        {
            populate_wifi_status_json(response);
            cJSON_AddBoolToObject(response, "ok", true);
        }
        else if (strcmp(cmd, "scan") == 0)
        {
            esp_err_t err = wifi_manager_start_scan(http_server_publish_scan_results);
            if (err == ESP_OK)
            {
                cJSON_AddBoolToObject(response, "ok", true);
                cJSON_AddStringToObject(response, "message", "Scan initiated");
            }
            else
            {
                cJSON_AddBoolToObject(response, "ok", false);
                cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
            }
        }
        else
        {
            cJSON_AddBoolToObject(response, "ok", false);
            cJSON_AddStringToObject(response, "err", "unknown_cmd");
        }
    }

    char *resp_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);

    free(resp_str);
    cJSON_Delete(response);
    cJSON_Delete(req_json);

    return ESP_OK;
}

// Captive portal handlers
static esp_err_t portal_page_handler(httpd_req_t *req)
{
    const char *hostname = wifi_manager_get_hostname();
    char host_url[96] = {0};
    char ip_fallback[16] = {0};
    char fallback_url[64] = {0};

    if (hostname && strlen(hostname) > 0)
    {
        snprintf(host_url, sizeof(host_url), "http://%s.local/", hostname);
    }

    if (wifi_manager_get_ip(ip_fallback, sizeof(ip_fallback)) != ESP_OK || strlen(ip_fallback) == 0)
    {
        strncpy(ip_fallback, "192.168.4.1", sizeof(ip_fallback) - 1);
    }
    snprintf(fallback_url, sizeof(fallback_url), "http://%s/", ip_fallback);

    const char *redirect_target = strlen(host_url) > 0 ? host_url : fallback_url;

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", redirect_target);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_type(req, "text/html");

    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head>"
                                  "<title>Captive Portal</title>"
                                  "<meta http-equiv=\"refresh\" content=\"0; url=");
    httpd_resp_send_chunk(req, redirect_target, strlen(redirect_target));
    httpd_resp_sendstr_chunk(req, "\"></head><body style=\"font-family:sans-serif;text-align:center;padding:40px;\">"
                                  "<h2>ESP32 Control Portal</h2>");

    if (strlen(host_url) > 0)
    {
        httpd_resp_sendstr_chunk(req, "<p>Redirecting to <a href=\"");
        httpd_resp_send_chunk(req, host_url, strlen(host_url));
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_send_chunk(req, host_url, strlen(host_url));
        httpd_resp_sendstr_chunk(req, "</a>.</p>");
    }

    httpd_resp_sendstr_chunk(req, "<p>If the redirect fails, try <a href=\"");
    httpd_resp_send_chunk(req, fallback_url, strlen(fallback_url));
    httpd_resp_sendstr_chunk(req, "\">");
    httpd_resp_send_chunk(req, fallback_url, strlen(fallback_url));
    httpd_resp_sendstr_chunk(req, "</a>.</p></body></html>");

    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

esp_err_t http_server_start(uint16_t port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.ctrl_port = port + 1;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    if (httpd_start(&s_server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    // Main page
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    };
    httpd_register_uri_handler(s_server, &root_uri);

    // API endpoint
    httpd_uri_t api_uri = {
        .uri = "/api",
        .method = HTTP_POST,
        .handler = api_handler,
    };
    httpd_register_uri_handler(s_server, &api_uri);

    // Captive portal detection URLs
    httpd_uri_t generate_204_uri = {
        .uri = "/generate_204", // Android
        .method = HTTP_GET,
        .handler = portal_page_handler,
    };
    httpd_register_uri_handler(s_server, &generate_204_uri);

    httpd_uri_t hotspot_detect_uri = {
        .uri = "/hotspot-detect.html", // iOS
        .method = HTTP_GET,
        .handler = portal_page_handler,
    };
    httpd_register_uri_handler(s_server, &hotspot_detect_uri);

    httpd_uri_t connecttest_uri = {
        .uri = "/connecttest.txt", // Windows
        .method = HTTP_GET,
        .handler = portal_page_handler,
    };
    httpd_register_uri_handler(s_server, &connecttest_uri);

    httpd_uri_t success_uri = {
        .uri = "/success.txt", // Firefox
        .method = HTTP_GET,
        .handler = portal_page_handler,
    };
    httpd_register_uri_handler(s_server, &success_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", port);
    s_captive_portal_enabled = false;

    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    http_server_disable_captive_portal();

    if (s_server)
    {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    return ESP_OK;
}

esp_err_t http_server_enable_captive_portal(void)
{
    if (!s_server)
    {
        ESP_LOGW(TAG, "Cannot enable captive portal before HTTP server start");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_captive_portal_enabled)
    {
        return ESP_OK;
    }

    esp_err_t err = dns_server_start();
    if (err == ESP_OK)
    {
        s_captive_portal_enabled = true;
        ESP_LOGI(TAG, "Captive portal enabled");
    }
    else if (err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to start DNS server for captive portal: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t http_server_disable_captive_portal(void)
{
    if (!s_captive_portal_enabled)
    {
        return ESP_OK;
    }

    esp_err_t err = dns_server_stop();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Captive portal disabled");
    }
    else if (err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to stop DNS server: %s", esp_err_to_name(err));
    }

    s_captive_portal_enabled = false;

    return err;
}
