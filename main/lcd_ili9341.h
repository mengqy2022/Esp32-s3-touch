#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t lcd_color_t;

#define LCD_RGB565(r, g, b) ((lcd_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define LCD_BLACK       LCD_RGB565(0, 0, 0)
#define LCD_WHITE       LCD_RGB565(255, 255, 255)
#define LCD_RED         LCD_RGB565(220, 40, 40)
#define LCD_GREEN       LCD_RGB565(40, 200, 90)
#define LCD_BLUE        LCD_RGB565(55, 120, 235)
#define LCD_YELLOW      LCD_RGB565(245, 190, 45)
#define LCD_GRAY        LCD_RGB565(120, 125, 135)
#define LCD_DARK        LCD_RGB565(24, 28, 36)
#define LCD_PANEL       LCD_RGB565(38, 44, 55)
#define LCD_CYAN        LCD_RGB565(45, 205, 210)

esp_err_t lcd_init(void);
void lcd_set_backlight(bool on);
void lcd_fill_screen(lcd_color_t color);
void lcd_fill_rect(int x, int y, int w, int h, lcd_color_t color);
void lcd_fill_circle(int cx, int cy, int r, lcd_color_t color);
void lcd_draw_rect(int x, int y, int w, int h, lcd_color_t color, int thickness);
void lcd_draw_text(int x, int y, const char *text, lcd_color_t fg, lcd_color_t bg, int scale);
void lcd_draw_crosshair(int x, int y, lcd_color_t color);
// Draw a raw RGB565 pixel buffer into a rectangular area (used by LVGL flush).
void lcd_draw_pixels(int x, int y, int w, int h, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
