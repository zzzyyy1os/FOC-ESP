/**
 * @file uart_test.c
 * @brief ESP32-S3 串口命令转发
 *        UART1(GPIO17=TX, GPIO18=RX) 电脑通讯
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

/* 电脑通讯UART (UART1, GPIO17=TX, GPIO18=RX, 115200) */
#define PC_UART_PORT       UART_NUM_1
#define PC_UART_TX_PIN     17
#define PC_UART_RX_PIN     18
#define PC_UART_BAUD       115200

/* STM32通讯UART (UART2, GPIO15=TX, GPIO16=RX, 1M) */
#define STM_UART_PORT      UART_NUM_2
#define STM_UART_TX_PIN    15
#define STM_UART_RX_PIN    16
#define STM_UART_BAUD      1000000

static void pc_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = PC_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(PC_UART_PORT, 1024, 1024, 0, NULL, 0);
    uart_param_config(PC_UART_PORT, &uart_config);
    uart_set_pin(PC_UART_PORT, PC_UART_TX_PIN, PC_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

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

void uart_cmd_task(void *arg)
{
    pc_uart_init();
    stm_uart_init();

    uint8_t rx_buf[256];
    int len;

    ESP_LOGI(TAG, "UART CMD Task Started");

    while (1)
    {
        /* 从电脑UART1读取 */
        len = uart_read_bytes(PC_UART_PORT, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(100));
        if (len > 0)
        {
            rx_buf[len] = '\0';

            /* 回显给电脑 */
            uart_write_bytes(PC_UART_PORT, rx_buf, len);

            /* 转发给STM32 */
            uart_write_bytes(STM_UART_PORT, rx_buf, len);

            /* 记录发送内容供OLED显示 */
            snprintf(stm_last_cmd, sizeof(stm_last_cmd), "%s", (char *)rx_buf);
            /* 去掉末尾换行符方便显示 */
            int slen = strlen(stm_last_cmd);
            while (slen > 0 && (stm_last_cmd[slen-1] == '\n' || stm_last_cmd[slen-1] == '\r'))
                stm_last_cmd[--slen] = '\0';
            stm_cmd_updated = 1;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
