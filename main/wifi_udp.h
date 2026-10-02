/**
 * @file wifi_udp.h
 * @brief WiFi AP + UDP 服务器 (移植自 esp32_drone)
 *        手机 APP 通过 WiFi 连接 ESP32-S3，使用 UDP 协议发送 CRTP 数据
 */
#ifndef __WIFI_UDP_H__
#define __WIFI_UDP_H__

#include <stdint.h>
#include <stdbool.h>

#define WIFI_UDP_PORT           2390
#define WIFI_UDP_BUFSIZE        128
#define WIFI_RX_TX_PACKET_SIZE  64

/* UDP 数据包结构体 (兼容 CRTP 协议) */
typedef struct {
    uint8_t size;
    uint8_t data[WIFI_RX_TX_PACKET_SIZE];
} UDPPacket;

/**
 * @brief 初始化 WiFi AP + UDP 服务器
 *        创建热点 "ESP-BRIDGE_XXXXXX"，密码 12345678
 *        启动 UDP 服务器监听端口 2390
 */
void wifi_udp_init(void);

/**
 * @brief 查询是否有手机客户端连接
 * @return true 表示已收到至少一个 UDP 数据包
 */
bool wifi_udp_is_connected(void);

/**
 * @brief 从接收队列获取一个 UDP 数据包
 * @param pkt   输出参数，存放收到的数据
 * @param timeout_ms 超时时间(毫秒)
 * @return true 表示成功收到数据，false 表示超时
 */
bool wifi_udp_receive(UDPPacket *pkt, int timeout_ms);

/**
 * @brief 发送数据到手机客户端
 * @param size  数据长度
 * @param data  数据指针
 * @return true 表示发送成功
 */
bool wifi_udp_send(uint32_t size, const uint8_t *data);

#endif /* __WIFI_UDP_H__ */
