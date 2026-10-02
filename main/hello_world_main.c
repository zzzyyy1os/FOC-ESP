/**
 * @file hello_world_main.c
 * @brief ESP32-S3 串口桥接器 (PC<->STM32) + WiFi AP + OLED显示
 *
 * 功能:
 *   1. WiFi AP 热点，手机 APP 通过 UDP 发送 CRTP 数据
 *   2. PC 通过 UART1 发送串口数据
 *   3. 两条路径的数据都通过 UART2 转发给 STM32
 *   4. OLED 显示 WiFi 状态和最近发送的命令
 */
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"

#include "oled.h"
#include "uart_test.h"
#include "wifi_udp.h"

static const char *TAG = "MAIN";

/* WiFi 收到的数据 (供OLED显示) */
static char wifi_last_data[256] = "";
static volatile bool wifi_data_updated = false;

/* OLED I2C引脚 - I2C1 */
#define OLED_I2C_PORT           I2C_NUM_1
#define OLED_I2C_SCL_IO         4
#define OLED_I2C_SDA_IO         3
#define OLED_I2C_FREQ_HZ       400000

/* 任务栈大小 */
#define OLED_TASK_STACK_SIZE    4096

/* I2C总线句柄 */
static i2c_master_bus_handle_t oled_i2c_bus_handle = NULL;

static void oled_i2c_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_I2C_SDA_IO,
        .scl_io_num = OLED_I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &oled_i2c_bus_handle));
}

/**
 * @brief OLED显示任务 (50Hz)
 *        显示 WiFi 信息和最近发送的命令
 */
static void oled_task(void *arg)
{
    OLED_Init(oled_i2c_bus_handle);

    /* 静态区域: 显示 WiFi 连接信息 */
    OLED_Clear();
    OLED_ShowString(0, 0, "WiFi: ESP-DRONE");
    OLED_ShowString(0, 16, "PW: 12345678");
    OLED_ShowString(0, 32, "UDP :2390");
    OLED_Display();

    bool last_connected = false;

    while (1) {
        /* 动态区域: 更新连接状态 */
        OLED_ClearArea(0, 32, 128, 8);
        if (wifi_udp_is_connected()) {
            OLED_ShowString(0, 32, "Status: Connected");
            if (!last_connected) {
                last_connected = true;
            }
        } else {
            OLED_ShowString(0, 32, "Waiting phone...");
            last_connected = false;
        }

        /* 动态区域: 显示 WiFi 收到的数据 */
        OLED_ClearArea(0, 48, 128, 8);
        OLED_ShowString(0, 48, "Rx:");
        if (strlen(wifi_last_data) > 0)
            OLED_ShowString(24, 48, wifi_last_data);

        OLED_Display();
        vTaskDelay(pdMS_TO_TICKS(20));  /* 50Hz */
    }
}

/**
 * @brief WiFi 数据接收任务
 *        从 WiFi UDP 队列取数据，在 OLED 上显示
 *        支持两种 Commander 格式:
 *        - 标准格式: header=0x30, 15字节, float little-endian
 *        - 旧版格式: header=0x80, 11字节, big-endian uint16
 */
static void wifi_rx_display_task(void *arg)
{
    UDPPacket pkt;

    ESP_LOGI(TAG, "WiFi RX Display Task Started");

    while (1) {
        if (wifi_udp_receive(&pkt, 100)) {
            uint8_t header = pkt.data[0];
            uint8_t port = (header >> 4) & 0x0F;
            uint8_t channel = header & 0x0F;

            ESP_LOGI(TAG, "RX: header=0x%02X port=0x%02X ch=%d size=%d",
                     header, port, channel, pkt.size);

            /* 标准 Commander 格式: header=0x30, size=15 */
            if (header == 0x30 && pkt.size >= 15) {
                float roll, pitch, yaw;
                uint16_t thrust;

                memcpy(&roll,   &pkt.data[1], 4);
                memcpy(&pitch,  &pkt.data[5], 4);
                memcpy(&yaw,    &pkt.data[9], 4);
                memcpy(&thrust, &pkt.data[13], 2);

                snprintf(wifi_last_data, sizeof(wifi_last_data),
                         "P:%.1f R:%.1f T:%d", pitch, roll, thrust);
                wifi_data_updated = true;

                ESP_LOGI(TAG, "  [STD] P=%.1f R=%.1f Y=%.1f T=%d",
                         pitch, roll, yaw, thrust);
            }
            /* 旧版 Commander 格式: header=0x80, size=11 */
            else if (header == 0x80 && pkt.size >= 11) {
                /* 旧版: big-endian uint16, 中心值296, 缩放 */
                uint16_t raw_roll  = (pkt.data[1] << 8) | pkt.data[2];
                uint16_t raw_pitch = (pkt.data[3] << 8) | pkt.data[4];
                uint16_t raw_thrust= (pkt.data[5] << 8) | pkt.data[6];
                uint16_t raw_yaw   = (pkt.data[7] << 8) | pkt.data[8];

                float roll  = (float)((int16_t)raw_roll  - 296) * 15.0f / 150.0f;
                float pitch = -(float)((int16_t)raw_pitch - 296) * 15.0f / 150.0f;
                float yaw   = (float)((int16_t)raw_yaw   - 296) * 15.0f / 150.0f;
                uint16_t thrust = (uint16_t)(raw_thrust * 59000.0f / 600.0f);

                snprintf(wifi_last_data, sizeof(wifi_last_data),
                         "P:%.1f R:%.1f T:%d", pitch, roll, thrust);
                wifi_data_updated = true;

                ESP_LOGI(TAG, "  [LEG] P=%.1f R=%.1f Y=%.1f T=%d",
                         pitch, roll, yaw, thrust);
            }
            /* 其他类型数据: 显示十六进制 */
            else {
                int pos = 0;
                for (int i = 0; i < pkt.size && pos < (int)sizeof(wifi_last_data) - 3; i++) {
                    pos += snprintf(wifi_last_data + pos, sizeof(wifi_last_data) - pos,
                                    "%02X", pkt.data[i]);
                }
                wifi_last_data[pos] = '\0';
                wifi_data_updated = true;

                ESP_LOGI(TAG, "  [HEX] %s", wifi_last_data);
            }
        }
    }
}

/**
 * @brief NVS 初始化 (WiFi 驱动需要)
 */
static void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    ESP_LOGI(TAG, "UART Bridge + WiFi Starting...");

    /* 1. NVS 初始化 (WiFi 驱动需要) */
    nvs_init();

    /* 2. I2C + OLED 初始化 */
    oled_i2c_init();

    /* 3. WiFi AP + UDP 初始化 */
    wifi_udp_init();

    /* 4. 创建任务 */
    xTaskCreate(oled_task, "OLEDTask", OLED_TASK_STACK_SIZE, NULL, 4, NULL);
    xTaskCreate(uart_cmd_task, "UARTCmd", 4096, NULL, 3, NULL);
    xTaskCreate(wifi_rx_display_task, "WiFiRX", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "All tasks created");
}
