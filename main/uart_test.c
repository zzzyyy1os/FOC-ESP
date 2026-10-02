/**
 * @file uart_test.c
 * @brief ESP32-S3 串口通讯
 *        UART1(GPIO17=TX, GPIO18=RX) 已改为调试串口 (ESP_LOG)
 *        UART2(GPIO15=TX, GPIO16=RX) STM32通讯
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "UART_CMD";

/* 最近发送给STM32的命令 (供OLED显示) */
char stm_last_cmd[256] = "";
volatile uint8_t stm_cmd_updated = 0;

/* STM32通讯UART (UART2, GPIO15=TX, GPIO16=RX, 1M) */
#define STM_UART_PORT      UART_NUM_2
#define STM_UART_TX_PIN    15
#define STM_UART_RX_PIN    16
#define STM_UART_BAUD      1000000

static void stm_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = STM_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(STM_UART_PORT, 1024, 1024, 0, NULL, 0);
    uart_param_config(STM_UART_PORT, &uart_config);
    uart_set_pin(STM_UART_PORT, STM_UART_TX_PIN, STM_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void uart_send_to_stm(const uint8_t *data, int len)
{
    if (len <= 0) return;

    /* 发送到 STM32 */
    uart_write_bytes(STM_UART_PORT, data, len);

    /* 记录发送内容供 OLED 显示 */
    int copy_len = (len < (int)sizeof(stm_last_cmd) - 1) ? len : (int)sizeof(stm_last_cmd) - 1;
    memcpy(stm_last_cmd, data, copy_len);
    stm_last_cmd[copy_len] = '\0';

    /* 去掉末尾换行符方便显示 */
    while (copy_len > 0 && (stm_last_cmd[copy_len - 1] == '\n' || stm_last_cmd[copy_len - 1] == '\r'))
        stm_last_cmd[--copy_len] = '\0';

    stm_cmd_updated = 1;
}

void uart_cmd_task(void *arg)
{
    /* UART1 已改为调试串口, 仅初始化 STM32 UART2 */
    stm_uart_init();

    ESP_LOGI(TAG, "STM32 UART2 Task Started (GPIO15=TX, GPIO16=RX, 1Mbaud)");

    /* UART1 现在用于 ESP_LOG 调试输出, 不再做 PC 串口桥接 */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
