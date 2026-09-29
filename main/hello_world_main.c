/**
 * @file hello_world_main.c
 * @brief ESP32-S3 双机串口通讯 + OLED显示
 */
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "oled.h"
#include "uart_test.h"
#include <string.h>

static const char *TAG = "MAIN";

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
 */
static void oled_task(void *arg)
{
    OLED_Init(oled_i2c_bus_handle);

    OLED_Clear();
    OLED_ShowString(0, 0, "UART Bridge");
    OLED_ShowString(0, 16, "PC <-> STM32");
    OLED_ShowString(0, 32, "1M 8N1");
    OLED_Display();

    uint32_t counter = 0;

    while (1) {
        /* 更新计数器显示 */
        OLED_ClearArea(0, 48, 128, 8);
        OLED_ShowString(0, 48, "Tx:");
        if (strlen(stm_last_cmd) > 0)
            OLED_ShowString(24, 48, stm_last_cmd);
        OLED_Display();

        counter++;
        vTaskDelay(pdMS_TO_TICKS(20));  /* 50Hz */
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "UART Bridge Starting...");

    /* 初始化I2C */
    oled_i2c_init();

    /* 创建任务 */
    xTaskCreate(oled_task, "OLEDTask", OLED_TASK_STACK_SIZE, NULL, 4, NULL);
    xTaskCreate(uart_cmd_task, "UARTCmd", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "All tasks created");
}
