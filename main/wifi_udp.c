/**
 * @file wifi_udp.c
 * @brief WiFi AP + UDP 服务器实现 (移植自 esp32_drone/wifi_esp32.c)
 *
 * 功能:
 *   1. 创建 WiFi 热点 (AP 模式)
 *   2. 启动 UDP 服务器监听端口 2390
 *   3. 接收手机 APP 发送的 CRTP 数据包 (带校验和)
 *   4. 验证校验和后放入队列供上层使用
 *   5. 支持向手机发送数据 (带校验和)
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "wifi_udp.h"

static const char *TAG = "WIFI_UDP";

/* WiFi AP 配置 */
static char wifi_ssid[32] = "ESP-BRIDGE";
static const char *wifi_pwd = "12345678";
static const uint8_t wifi_channel = 1;
#define MAX_STA_CONN    3

/* UDP 服务器配置 */
#define UDP_SERVER_PORT     2390

/* 内部变量 */
static struct sockaddr_in6 source_addr;
static struct sockaddr_in dest_addr;
static int sock = -1;
static char rx_buffer[WIFI_UDP_BUFSIZE];
static char tx_buffer[WIFI_UDP_BUFSIZE];

static QueueHandle_t udpDataRx = NULL;
static QueueHandle_t udpDataTx = NULL;
static UDPPacket inPacket;
static UDPPacket outPacket;

static bool isInit = false;
static bool isUDPInit = false;
static bool isUDPConnected = false;

/* 前向声明 */
static esp_err_t udp_server_create(void);

/**
 * @brief 计算校验和 (简单字节累加，兼容 CRTP 协议)
 */
static uint8_t calculate_cksum(const void *data, size_t len)
{
    const unsigned char *c = (const unsigned char *)data;
    uint8_t cksum = 0;
    for (size_t i = 0; i < len; i++) {
        cksum += c[i];
    }
    return cksum;
}

/**
 * @brief WiFi 事件处理回调
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station connected, AID=%d", event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station disconnected, AID=%d", event->aid);
    }
}

/**
 * @brief 创建 UDP 服务器 socket 并绑定端口
 */
static esp_err_t udp_server_create(void)
{
    if (isUDPInit) {
        return ESP_OK;
    }

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_SERVER_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Socket created");

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", UDP_SERVER_PORT);

    isUDPInit = true;
    return ESP_OK;
}

/**
 * @brief 处理 CRTP 握手请求并生成响应
 *        Port 0x0F, Ch 0: Echo (原样返回)
 *        Port 0x0F, Ch 1: Source (返回 "Bitcraze Crazyflie")
 */
static void crtp_handle_packet(const UDPPacket *pkt)
{
    if (pkt->size < 1) return;

    uint8_t header = pkt->data[0];
    uint8_t port = (header >> 4) & 0x0F;
    uint8_t channel = header & 0x0F;

    ESP_LOGI(TAG, "CRTP: port=0x%02X ch=%d size=%d", port, channel, pkt->size);

    if (port == 0x0F) {
        UDPPacket resp;
        memset(&resp, 0, sizeof(resp));

        if (channel == 0) {
            /* Echo: 原样返回 */
            memcpy(resp.data, pkt->data, pkt->size);
            resp.size = pkt->size;
            ESP_LOGI(TAG, "CRTP Echo request -> echoing back");
        } else if (channel == 1) {
            /* Source: 返回设备名称 */
            const char *source = "Bitcraze Crazyflie";
            resp.data[0] = header;
            resp.size = 1 + strlen(source);
            memcpy(&resp.data[1], source, strlen(source));
            ESP_LOGI(TAG, "CRTP Source request -> replying");
        } else {
            return;
        }

        /* 发送响应: resp.data[0..size-1] + checksum */
        xQueueSend(udpDataTx, &resp, pdMS_TO_TICKS(2));
    }
}

/**
 * @brief UDP 接收任务
 *        从 socket 接收数据，验证校验和后放入接收队列
 *        同时处理 CRTP 握手请求
 */
static void udp_server_rx_task(void *pvParameters)
{
    uint8_t cksum = 0;
    socklen_t socklen = sizeof(source_addr);

    while (true) {
        if (!isUDPInit) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                           (struct sockaddr *)&source_addr, &socklen);
        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        } else if (len > WIFI_RX_TX_PACKET_SIZE - 4) {
            ESP_LOGW(TAG, "Received data length = %d > 64", len);
        } else {
            rx_buffer[len] = 0;
            memcpy(inPacket.data, rx_buffer, len);
            cksum = inPacket.data[len - 1];
            /* 移除校验和字节，不属于 CRTP 数据 */
            inPacket.size = len - 1;

            /* 验证校验和 */
            if (cksum == calculate_cksum(inPacket.data, len - 1) && inPacket.size < 64) {
                if (!isUDPConnected) {
                    isUDPConnected = true;
                    ESP_LOGI(TAG, "Phone client connected!");
                }

                /* 处理 CRTP 握手请求 (Port 0x0F) */
                crtp_handle_packet(&inPacket);

                /* 放入接收队列供上层使用 */
                xQueueSend(udpDataRx, &inPacket, pdMS_TO_TICKS(2));
            } else {
                ESP_LOGW(TAG, "UDP packet checksum mismatch (recv cksum=0x%02X, calc=0x%02X)",
                         cksum, calculate_cksum(inPacket.data, len - 1));
            }
        }
    }
}

/**
 * @brief UDP 发送任务
 *        从发送队列取出数据，添加校验和后通过 socket 发送
 */
static void udp_server_tx_task(void *pvParameters)
{
    while (true) {
        if (!isUDPInit) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if ((xQueueReceive(udpDataTx, &outPacket, pdMS_TO_TICKS(5)) == pdTRUE) && isUDPConnected) {
            memcpy(tx_buffer, outPacket.data, outPacket.size);
            tx_buffer[outPacket.size] = calculate_cksum(tx_buffer, outPacket.size);
            tx_buffer[outPacket.size + 1] = 0;

            int err = sendto(sock, tx_buffer, outPacket.size + 1, 0,
                             (struct sockaddr *)&source_addr, sizeof(source_addr));
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                continue;
            }
        }
    }
}

/* ========== 公共 API ========== */

void wifi_udp_init(void)
{
    if (isInit) {
        return;
    }

    esp_netif_t *ap_netif = NULL;
    uint8_t mac[6];

    /* 初始化网络接口和事件循环 */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ap_netif = esp_netif_create_default_wifi_ap();

    /* 初始化 WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册 WiFi 事件处理 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    /* 获取 MAC 地址，拼接到 SSID */
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_AP, mac));
    snprintf(wifi_ssid, sizeof(wifi_ssid), "ESP-DRONE_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* 配置 WiFi AP */
    wifi_config_t wifi_config = {
        .ap = {
            .channel = wifi_channel,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    memcpy(wifi_config.ap.ssid, wifi_ssid, strlen(wifi_ssid) + 1);
    wifi_config.ap.ssid_len = strlen(wifi_ssid);
    memcpy(wifi_config.ap.password, wifi_pwd, strlen(wifi_pwd) + 1);

    if (strlen(wifi_pwd) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 设置静态 IP (与 ESP-Drone APP 兼容) */
    esp_netif_ip_info_t ip_info = {
        .ip.addr = ipaddr_addr("192.168.43.42"),
        .netmask.addr = ipaddr_addr("255.255.255.0"),
        .gw.addr = ipaddr_addr("192.168.43.42"),
    };
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    ESP_LOGI(TAG, "WiFi AP started. SSID:%s Password:%s", wifi_ssid, wifi_pwd);

    /* 创建收发队列 */
    udpDataRx = xQueueCreate(5, sizeof(UDPPacket));
    udpDataTx = xQueueCreate(5, sizeof(UDPPacket));

    /* 创建 UDP 服务器 */
    if (udp_server_create() != ESP_OK) {
        ESP_LOGE(TAG, "UDP server create failed!");
    } else {
        ESP_LOGI(TAG, "UDP server create succeed!");
    }

    /* 创建收发任务 */
    xTaskCreate(udp_server_tx_task, "udp_tx", 4096, NULL, 5, NULL);
    xTaskCreate(udp_server_rx_task, "udp_rx", 4096, NULL, 5, NULL);

    isInit = true;
}

bool wifi_udp_is_connected(void)
{
    return isUDPConnected;
}

bool wifi_udp_receive(UDPPacket *pkt, int timeout_ms)
{
    if (udpDataRx == NULL) return false;
    return (xQueueReceive(udpDataRx, pkt, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

bool wifi_udp_send(uint32_t size, const uint8_t *data)
{
    if (udpDataTx == NULL) return false;

    static UDPPacket outStage;
    outStage.size = size;
    memcpy(outStage.data, data, size);
    return (xQueueSend(udpDataTx, &outStage, pdMS_TO_TICKS(100)) == pdTRUE);
}
