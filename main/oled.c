/**
 * @file oled.c
 * @brief SSD1306 OLED驱动 - ESP32-S3移植版
 * @note  使用ESP-IDF I2C Master驱动
 */
#include "oled.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "OLED";

/* SSD1306 commands */
#define SSD1306_CMD_SET_MUX_RATIO      0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET  0xD3
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_SET_SEGMENT_REMAP   0xA1
#define SSD1306_CMD_SET_COM_SCAN_DIR    0xC8
#define SSD1306_CMD_SET_COM_PINS        0xDA
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_DISABLE_ENTIRE_ON   0xA4
#define SSD1306_CMD_SET_NORMAL_DISPLAY  0xA6
#define SSD1306_CMD_SET_OSC_FREQ        0xD5
#define SSD1306_CMD_SET_CHARGE_PUMP     0x8D
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOMH_DESELECT  0xDB
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_SET_PAGE_ADDR       0x22
#define SSD1306_CMD_SET_COL_ADDR        0x21
#define SSD1306_CMD_SET_MEMORY_MODE     0x20
#define SSD1306_CMD_SET_SCROLL          0x2E

/* Control byte */
#define OLED_CONTROL_CMD    0x00
#define OLED_CONTROL_DATA   0x40

/* Private variables */
static i2c_master_dev_handle_t oled_handle = NULL;
static uint8_t oled_buffer[OLED_PAGES][OLED_WIDTH];

/* 6x8 font table (ASCII 32-126) */
static const uint8_t oled_font_6x8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, /* sp */
    {0x00,0x00,0x5F,0x00,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, /* $ */
    {0x23,0x13,0x08,0x64,0x62,0x00}, /* % */
    {0x36,0x49,0x55,0x22,0x50,0x00}, /* & */
    {0x00,0x05,0x03,0x00,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14,0x00}, /* * */
    {0x08,0x08,0x3E,0x08,0x08,0x00}, /* + */
    {0x00,0x50,0x30,0x00,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08,0x00}, /* - */
    {0x00,0x60,0x60,0x00,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02,0x00}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46,0x00}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31,0x00}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10,0x00}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39,0x00}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03,0x00}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36,0x00}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E,0x00}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14,0x00}, /* = */
    {0x00,0x41,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06,0x00}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E,0x00}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, /* A */
    {0x7F,0x49,0x49,0x49,0x36,0x00}, /* B */
    {0x3E,0x41,0x41,0x41,0x22,0x00}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, /* D */
    {0x7F,0x49,0x49,0x49,0x41,0x00}, /* E */
    {0x7F,0x09,0x09,0x09,0x01,0x00}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A,0x00}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, /* H */
    {0x00,0x41,0x7F,0x41,0x00,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01,0x00}, /* J */
    {0x7F,0x08,0x14,0x22,0x41,0x00}, /* K */
    {0x7F,0x40,0x40,0x40,0x40,0x00}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F,0x00}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, /* O */
    {0x7F,0x09,0x09,0x09,0x06,0x00}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46,0x00}, /* R */
    {0x46,0x49,0x49,0x49,0x31,0x00}, /* S */
    {0x01,0x01,0x7F,0x01,0x01,0x00}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F,0x00}, /* W */
    {0x63,0x14,0x08,0x14,0x63,0x00}, /* X */
    {0x07,0x08,0x70,0x08,0x07,0x00}, /* Y */
    {0x61,0x51,0x49,0x45,0x43,0x00}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20,0x00}, /* \ */
    {0x00,0x41,0x41,0x7F,0x00,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04,0x00}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* _ */
    {0x00,0x01,0x02,0x04,0x00,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78,0x00}, /* a */
    {0x7F,0x48,0x44,0x44,0x38,0x00}, /* b */
    {0x38,0x44,0x44,0x44,0x20,0x00}, /* c */
    {0x38,0x44,0x44,0x48,0x7F,0x00}, /* d */
    {0x38,0x54,0x54,0x54,0x18,0x00}, /* e */
    {0x08,0x7E,0x09,0x01,0x02,0x00}, /* f */
    {0x0C,0x52,0x52,0x52,0x3E,0x00}, /* g */
    {0x7F,0x08,0x04,0x04,0x78,0x00}, /* h */
    {0x00,0x44,0x7D,0x40,0x00,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78,0x00}, /* m */
    {0x7C,0x08,0x04,0x04,0x78,0x00}, /* n */
    {0x38,0x44,0x44,0x44,0x38,0x00}, /* o */
    {0x7C,0x14,0x14,0x14,0x08,0x00}, /* p */
    {0x08,0x14,0x14,0x18,0x7C,0x00}, /* q */
    {0x7C,0x08,0x04,0x04,0x08,0x00}, /* r */
    {0x48,0x54,0x54,0x54,0x20,0x00}, /* s */
    {0x04,0x3F,0x44,0x40,0x20,0x00}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, /* w */
    {0x44,0x28,0x10,0x28,0x44,0x00}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, /* y */
    {0x44,0x64,0x54,0x4C,0x44,0x00}, /* z */
    {0x00,0x08,0x36,0x41,0x00,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00,0x00}, /* } */
    {0x10,0x08,0x08,0x10,0x08,0x00}, /* ~ */
};

/* Private function: send command */
static esp_err_t OLED_WriteCmd(uint8_t cmd)
{
    uint8_t buf[2] = {OLED_CONTROL_CMD, cmd};
    return i2c_master_transmit(oled_handle, buf, 2, 100);
}

/* Private function: send data */
static esp_err_t OLED_WriteData(uint8_t *data, uint16_t size)
{
    /* Send in chunks with control byte prefix */
    uint8_t buf[129];
    buf[0] = OLED_CONTROL_DATA;
    uint16_t sent = 0;
    while (sent < size) {
        uint16_t chunk = size - sent;
        if (chunk > 128) chunk = 128;
        memcpy(&buf[1], &data[sent], chunk);
        esp_err_t ret = i2c_master_transmit(oled_handle, buf, chunk + 1, 100);
        if (ret != ESP_OK) return ret;
        sent += chunk;
    }
    return ESP_OK;
}

/* Initialize OLED (SSD1306 128x64) */
void OLED_Init(i2c_master_bus_handle_t bus_handle)
{
    /* Add OLED device to I2C bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &oled_handle));

    vTaskDelay(pdMS_TO_TICKS(100)); /* Wait for OLED power stable */

    OLED_WriteCmd(SSD1306_CMD_DISPLAY_OFF);             /* 0xAE Display OFF */
    OLED_WriteCmd(SSD1306_CMD_SET_MUX_RATIO);           /* 0xA8 */
    OLED_WriteCmd(0x3F);                                 /* 64 MUX */
    OLED_WriteCmd(SSD1306_CMD_SET_DISPLAY_OFFSET);      /* 0xD3 */
    OLED_WriteCmd(0x00);                                 /* No offset */
    OLED_WriteCmd(SSD1306_CMD_SET_START_LINE | 0x00);   /* 0x40 Start line = 0 */
    OLED_WriteCmd(SSD1306_CMD_SET_SEGMENT_REMAP | 0x01);/* 0xA1 Segment remap (flip horizontal) */
    OLED_WriteCmd(SSD1306_CMD_SET_COM_SCAN_DIR);        /* 0xC8 COM scan direction (flip vertical) */
    OLED_WriteCmd(SSD1306_CMD_SET_COM_PINS);            /* 0xDA */
    OLED_WriteCmd(0x12);                                 /* Alternative COM pin config */
    OLED_WriteCmd(SSD1306_CMD_SET_CONTRAST);            /* 0x81 */
    OLED_WriteCmd(0xCF);                                 /* Contrast = 0xCF */
    OLED_WriteCmd(SSD1306_CMD_SET_OSC_FREQ);            /* 0xD5 */
    OLED_WriteCmd(0x80);                                 /* Default clock */
    OLED_WriteCmd(SSD1306_CMD_SET_CHARGE_PUMP);         /* 0x8D */
    OLED_WriteCmd(0x14);                                 /* Enable charge pump */
    OLED_WriteCmd(SSD1306_CMD_SET_PRECHARGE);           /* 0xD9 */
    OLED_WriteCmd(0xF1);                                 /* Precharge period */
    OLED_WriteCmd(SSD1306_CMD_SET_VCOMH_DESELECT);      /* 0xDB */
    OLED_WriteCmd(0x30);                                 /* VCOMH deselect level */
    OLED_WriteCmd(SSD1306_CMD_DISABLE_ENTIRE_ON);       /* 0xA4 Resume to RAM content */
    OLED_WriteCmd(SSD1306_CMD_SET_NORMAL_DISPLAY);      /* 0xA6 Normal display (not inverted) */
    OLED_WriteCmd(SSD1306_CMD_SET_MEMORY_MODE);         /* 0x20 */
    OLED_WriteCmd(0x00);                                 /* Horizontal addressing mode */
    OLED_WriteCmd(SSD1306_CMD_SET_SCROLL);              /* 0x2E Deactivate scroll */
    OLED_WriteCmd(SSD1306_CMD_DISPLAY_ON);              /* 0xAF Display ON */

    OLED_Clear();
    OLED_Display();

    ESP_LOGI(TAG, "OLED initialized successfully");
}

/* Clear buffer */
void OLED_Clear(void)
{
    memset(oled_buffer, 0x00, sizeof(oled_buffer));
}

/* Send buffer to OLED */
void OLED_Display(void)
{
    OLED_WriteCmd(SSD1306_CMD_SET_COL_ADDR);
    OLED_WriteCmd(0);
    OLED_WriteCmd(OLED_WIDTH - 1);
    OLED_WriteCmd(SSD1306_CMD_SET_PAGE_ADDR);
    OLED_WriteCmd(0);
    OLED_WriteCmd(OLED_PAGES - 1);

    OLED_WriteData(&oled_buffer[0][0], OLED_PAGES * OLED_WIDTH);
}

/* Set pixel in buffer */
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;

    uint8_t page = y / 8;
    uint8_t bit_pos = y % 8;

    if (color)
        oled_buffer[page][x] |= (1 << bit_pos);
    else
        oled_buffer[page][x] &= ~(1 << bit_pos);
}

/* Clear a rectangular area in buffer */
void OLED_ClearArea(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    for (uint8_t i = 0; i < width; i++) {
        for (uint8_t j = 0; j < height; j++) {
            if ((x + i) < OLED_WIDTH && (y + j) < OLED_HEIGHT) {
                OLED_SetPixel(x + i, y + j, 0);
            }
        }
    }
}

/* Show a character at (x, y) - y is pixel row (0-63) */
static void OLED_ShowChar(uint8_t x, uint8_t y, char ch)
{
    if (ch < 32 || ch > 126) ch = ' ';
    uint8_t idx = ch - 32;

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t line = oled_font_6x8[idx][i];
        for (uint8_t j = 0; j < 8; j++) {
            if (line & (1 << j))
                OLED_SetPixel(x + i, y + j, 1);
        }
    }
}

/* Show string at (x, y) - y is pixel row */
void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
    while (*str) {
        if (x + OLED_FONT_WIDTH > OLED_WIDTH) {
            x = 0;
            y += OLED_FONT_HEIGHT;
        }
        if (y + OLED_FONT_HEIGHT > OLED_HEIGHT) break;

        OLED_ShowChar(x, y, *str);
        x += OLED_FONT_WIDTH;
        str++;
    }
}

/* Show integer number at (x, y) */
void OLED_ShowNum(uint8_t x, uint8_t y, int32_t num, uint8_t len)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%*ld", (int)len, (long)num);
    OLED_ShowString(x, y, buf);
}

/* Show float number at (x, y) */
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t intLen, uint8_t decLen)
{
    char buf[20];
    int pos = 0;

    /* Handle negative numbers */
    if (num < 0) {
        buf[pos++] = '-';
        num = -num;
    }

    /* Extract integer part */
    int32_t intPart = (int32_t)num;
    float fracPart = num - intPart;

    /* Convert integer part to string */
    char intBuf[12];
    int intPos = 0;
    if (intPart == 0) {
        intBuf[intPos++] = '0';
    } else {
        while (intPart > 0) {
            intBuf[intPos++] = '0' + (intPart % 10);
            intPart /= 10;
        }
    }

    /* Add leading spaces for alignment */
    int spaces = intLen - intPos;
    while (spaces-- > 0) {
        buf[pos++] = ' ';
    }

    /* Reverse integer digits */
    while (intPos > 0) {
        buf[pos++] = intBuf[--intPos];
    }

    /* Add decimal point */
    buf[pos++] = '.';

    /* Convert decimal part */
    for (int i = 0; i < decLen; i++) {
        fracPart *= 10;
        int digit = (int)fracPart;
        buf[pos++] = '0' + digit;
        fracPart -= digit;
    }

    buf[pos] = '\0';
    OLED_ShowString(x, y, buf);
}

/* Draw BMP picture */
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, const uint8_t *bmp)
{
    uint8_t x, y;
    uint16_t idx = 0;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            uint8_t byte = bmp[idx / 8];
            uint8_t bit = idx % 8;
            OLED_SetPixel(x, y, (byte >> bit) & 0x01);
            idx++;
        }
    }
}
