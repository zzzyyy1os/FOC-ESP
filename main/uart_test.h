#ifndef __UART_TEST_H__
#define __UART_TEST_H__

#include <stdint.h>

/* 最近发送给STM32的命令 (供OLED显示) */
extern char stm_last_cmd[256];
extern volatile uint8_t stm_cmd_updated;

/* 串口命令转发任务 */
void uart_cmd_task(void *arg);

/**
 * @brief 发送数据到 STM32 (供 WiFi 模块调用)
 * @param data  数据指针
 * @param len   数据长度
 */
void uart_send_to_stm(const uint8_t *data, int len);

#endif
