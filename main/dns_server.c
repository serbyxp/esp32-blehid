#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "DNS_SERVER";

#define DNS_PORT 53
#define DNS_MAX_PACKET_SIZE 512

static int s_sock = -1;
static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;

// DNS header structure
typedef struct
{
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static void dns_server_task(void *pvParameters)
{
    char rx_buffer[DNS_MAX_PACKET_SIZE];
    char tx_buffer[DNS_MAX_PACKET_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    ESP_LOGI(TAG, "DNS server task started");

    while (s_running)
    {
        int len = recvfrom(s_sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                           (struct sockaddr *)&client_addr, &client_addr_len);

        if (len > 0)
        {
            dns_header_t *header = (dns_header_t *)rx_buffer;

            // Only respond to queries
            if ((ntohs(header->flags) & 0x8000) == 0)
            {
                // Build response
                memcpy(tx_buffer, rx_buffer, len);
                dns_header_t *resp_header = (dns_header_t *)tx_buffer;

                // Set response flags
                resp_header->flags = htons(0x8180); // Standard query response, no error
                resp_header->ancount = htons(1);    // One answer
                resp_header->nscount = 0;
                resp_header->arcount = 0;

                int response_len = len;

                // Add answer section (point to question, type A, class IN, TTL 60s, IP 192.168.4.1)
                uint8_t *answer = (uint8_t *)(tx_buffer + len);

                // Name pointer to question
                answer[0] = 0xC0;
                answer[1] = 0x0C;

                // Type A (1)
                answer[2] = 0x00;
                answer[3] = 0x01;

                // Class IN (1)
                answer[4] = 0x00;
                answer[5] = 0x01;

                // TTL (60 seconds)
                answer[6] = 0x00;
                answer[7] = 0x00;
                answer[8] = 0x00;
                answer[9] = 0x3C;

                // Data length (4 bytes for IPv4)
                answer[10] = 0x00;
                answer[11] = 0x04;

                // IP address: 192.168.4.1
                answer[12] = 192;
                answer[13] = 168;
                answer[14] = 4;
                answer[15] = 1;

                response_len += 16;

                sendto(s_sock, tx_buffer, response_len, 0,
                       (struct sockaddr *)&client_addr, client_addr_len);
            }
        }
        else if (len < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "DNS server task stopped");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    if (s_running)
    {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }

    // Create UDP socket
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set socket to non-blocking
    int flags = fcntl(s_sock, F_GETFL, 0);
    fcntl(s_sock, F_SETFL, flags | O_NONBLOCK);

    // Bind to DNS port
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(DNS_PORT),
    };

    if (bind(s_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    s_running = true;
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_task_handle);

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);
    return ESP_OK;
}

esp_err_t dns_server_stop(void)
{
    if (!s_running)
    {
        return ESP_OK;
    }

    s_running = false;

    if (s_sock >= 0)
    {
        close(s_sock);
        s_sock = -1;
    }

    if (s_task_handle)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_task_handle = NULL;
    }

    ESP_LOGI(TAG, "DNS server stopped");
    return ESP_OK;
}
