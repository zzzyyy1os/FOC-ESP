#ifndef __OLED_H__
#define __OLED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "driver/i2c_master.h"

/* OLED I2C address (7-bit: 0x3C) */
#define OLED_I2C_ADDR       0x3C

/* OLED dimensions */
#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_PAGES          (OLED_HEIGHT / 8)

/* Font size */
#define OLED_FONT_WIDTH     6
#define OLED_FONT_HEIGHT    8

/* Function prototypes */
void OLED_Init(i2c_master_bus_handle_t bus_handle);
void OLED_Clear(void);
void OLED_Display(void);
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t color);
void OLED_ShowString(uint8_t x, uint8_t y, const char *str);
void OLED_ShowNum(uint8_t x, uint8_t y, int32_t num, uint8_t len);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t intLen, uint8_t decLen);
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, const uint8_t *bmp);
void OLED_ClearArea(uint8_t x, uint8_t y, uint8_t width, uint8_t height);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H__ */
