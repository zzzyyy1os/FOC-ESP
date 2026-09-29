#ifndef __UART_TEST_H__
#define __UART_TEST_H__

#include <stdint.h>

/* 最近发送给STM32的命令 (供OLED显示) */
extern char stm_last_cmd[256];
extern volatile uint8_t stm_cmd_updated;

/* 串口命令转发任务 */
void uart_cmd_task(void *arg);

#endif
