#include "lcd_ili9341.h"
#include "board_pins.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "lcd";
static spi_device_handle_t s_lcd;

#define LCD_SPI_CLOCK_HZ      (40 * 1000 * 1000)
#define LCD_SPI_MAX_TRANSFER  (32 * 1024)

static esp_err_t lcd_tx(bool data_mode, const void *data, size_t len)
{
    if (len == 0) return ESP_OK;
    gpio_set_level(PIN_LCD_DC, data_mode ? 1 : 0);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_lcd, &t);
}

static esp_err_t lcd_cmd(uint8_t cmd)
{
    return lcd_tx(false, &cmd, 1);
}

static esp_err_t lcd_data(const void *data, size_t len)
{
    return lcd_tx(true, data, len);
}

static void lcd_write_reg(uint8_t cmd, const uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(lcd_cmd(cmd));
    if (len) ESP_ERROR_CHECK(lcd_data(data, len));
}

static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t d[4];
    d[0] = (uint8_t)(x0 >> 8); d[1] = (uint8_t)x0;
    d[2] = (uint8_t)(x1 >> 8); d[3] = (uint8_t)x1;
    lcd_write_reg(0x2A, d, 4); // CASET
    d[0] = (uint8_t)(y0 >> 8); d[1] = (uint8_t)y0;
    d[2] = (uint8_t)(y1 >> 8); d[3] = (uint8_t)y1;
    lcd_write_reg(0x2B, d, 4); // PASET
    ESP_ERROR_CHECK(lcd_cmd(0x2C)); // RAMWR
}

esp_err_t lcd_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_LCD_BL, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .sclk_io_num = PIN_LCD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_SPI_MAX_TRANSFER,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = PIN_LCD_CS,
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_lcd));

    // Software reset because the LCD RESET pin shares the board RESET net.
    lcd_cmd(0x01);
    esp_rom_delay_us(150000);

    // ILI9341 initialization, conservative SPI settings.
    const uint8_t pwrb[] = {0x00, 0xC1, 0x30};
    const uint8_t pwrseq[] = {0x64, 0x03, 0x12, 0x81};
    const uint8_t dtca[] = {0x85, 0x00, 0x78};
    const uint8_t pwr_a[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
    const uint8_t prc[] = {0x20};
    const uint8_t dtcb[] = {0x00, 0x00};
    const uint8_t pw1[] = {0x23};
    const uint8_t pw2[] = {0x10};
    const uint8_t vm1[] = {0x3E, 0x28};
    const uint8_t vm2[] = {0x86};
    const uint8_t madctl[] = {0x88}; // landscape 320x240 native panel, MY=1 (user-confirmed orientation), BGR
    const uint8_t pixfmt[] = {0x55}; // RGB565
    const uint8_t frm[] = {0x00, 0x18};
    const uint8_t dfc[] = {0x08, 0x82, 0x27};
    const uint8_t f2[] = {0x00};
    const uint8_t gamma[] = {0x01};
    const uint8_t pgamma[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
    const uint8_t ngamma[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};

    lcd_write_reg(0xCF, pwrb, sizeof(pwrb));
    lcd_write_reg(0xED, pwrseq, sizeof(pwrseq));
    lcd_write_reg(0xE8, dtca, sizeof(dtca));
    lcd_write_reg(0xCB, pwr_a, sizeof(pwr_a));
    lcd_write_reg(0xF7, prc, sizeof(prc));
    lcd_write_reg(0xEA, dtcb, sizeof(dtcb));
    lcd_write_reg(0xC0, pw1, sizeof(pw1));
    lcd_write_reg(0xC1, pw2, sizeof(pw2));
    lcd_write_reg(0xC5, vm1, sizeof(vm1));
    lcd_write_reg(0xC7, vm2, sizeof(vm2));
    lcd_write_reg(0x36, madctl, sizeof(madctl));
    lcd_write_reg(0x3A, pixfmt, sizeof(pixfmt));
    lcd_write_reg(0xB1, frm, sizeof(frm));
    lcd_write_reg(0xB6, dfc, sizeof(dfc));
    lcd_write_reg(0xF2, f2, sizeof(f2));
    lcd_write_reg(0x26, gamma, sizeof(gamma));
    lcd_write_reg(0xE0, pgamma, sizeof(pgamma));
    lcd_write_reg(0xE1, ngamma, sizeof(ngamma));
    lcd_cmd(0x11); // sleep out
    esp_rom_delay_us(120000);
    lcd_cmd(0x29); // display on
    esp_rom_delay_us(20000);

    lcd_set_backlight(true);
    lcd_fill_screen(LCD_BLACK);
    ESP_LOGI(TAG, "ILI9341 initialized: 320x240 landscape");
    return ESP_OK;
}

void lcd_set_backlight(bool on)
{
    gpio_set_level(PIN_LCD_BL, on ? 1 : 0);
}

void lcd_draw_pixels(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (w <= 0 || h <= 0 || !pixels) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    if (w <= 0 || h <= 0) return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    // The LVGL draw buffer is DMA-capable internal RAM. Send a whole flush
    // area in one SPI transaction instead of dozens of 512-byte transactions.
    // CONFIG_LV_COLOR_16_SWAP already prepares RGB565 byte order for the panel.
    size_t bytes = (size_t)w * h * sizeof(uint16_t);
    ESP_ERROR_CHECK(lcd_data(pixels, bytes));
}

void lcd_fill_rect(int x, int y, int w, int h, lcd_color_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    if (w <= 0 || h <= 0) return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    uint8_t buf[512]; // 256 pixels
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;
    for (size_t i = 0; i < sizeof(buf); i += 2) {
        buf[i] = hi;
        buf[i + 1] = lo;
    }
    size_t pixels = (size_t)w * h;
    while (pixels) {
        size_t n = pixels > 256 ? 256 : pixels;
        ESP_ERROR_CHECK(lcd_data(buf, n * 2));
        pixels -= n;
    }
}

void lcd_fill_screen(lcd_color_t color)
{
    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, color);
}

void lcd_fill_circle(int cx, int cy, int r, lcd_color_t color)
{
    if (r <= 0) return;
    for (int dy = -r; dy <= r; ++dy) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        lcd_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

void lcd_draw_rect(int x, int y, int w, int h, lcd_color_t color, int thickness)
{
    if (thickness < 1) thickness = 1;
    lcd_fill_rect(x, y, w, thickness, color);
    lcd_fill_rect(x, y + h - thickness, w, thickness, color);
    lcd_fill_rect(x, y, thickness, h, color);
    lcd_fill_rect(x + w - thickness, y, thickness, h, color);
}

// Compact 5x7 font, ASCII 0x20..0x7F. Each byte is one vertical column, bit0 at top.
static const uint8_t font5x7[96][5] = {
{0,0,0,0,0},{0,0,0x5f,0,0},{0,0x07,0,0x07,0},{0x14,0x7f,0x14,0x7f,0x14},{0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,0x05,0x03,0,0},{0,0x1c,0x22,0x41,0},{0,0x41,0x22,0x1c,0},{0x14,0x08,0x3e,0x08,0x14},{0x08,0x08,0x3e,0x08,0x08},{0,0x50,0x30,0,0},{0x08,0x08,0x08,0x08,0x08},{0,0x60,0x60,0,0},{0x20,0x10,0x08,0x04,0x02},
{0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},{0x08,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
{0x32,0x49,0x79,0x41,0x3e},{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0,0x7f,0x41,0x41,0},{0x02,0x04,0x08,0x10,0x20},{0,0x41,0x41,0x7f,0},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
{0,0x01,0x02,0x04,0},{0x20,0x54,0x54,0x54,0x78},{0x7f,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7f},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7e,0x09,0x01,0x02},{0x0c,0x52,0x52,0x52,0x3e},{0x7f,0x08,0x04,0x04,0x78},{0,0x44,0x7d,0x40,0},{0x20,0x40,0x44,0x3d,0},{0x7f,0x10,0x28,0x44,0},{0,0x41,0x7f,0x40,0},{0x7c,0x04,0x18,0x04,0x78},{0x7c,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
{0x7c,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7c},{0x7c,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3f,0x44,0x40,0x20},{0x3c,0x40,0x40,0x20,0x7c},{0x1c,0x20,0x40,0x20,0x1c},{0x3c,0x40,0x30,0x40,0x3c},{0x44,0x28,0x10,0x28,0x44},{0x0c,0x50,0x50,0x50,0x3c},{0x44,0x64,0x54,0x4c,0x44},{0,0x08,0x36,0x41,0},{0,0,0x7f,0,0},{0,0x41,0x36,0x08,0},{0x08,0x04,0x08,0x10,0x08},{0,0,0,0,0}
};

static void lcd_draw_char(int x, int y, char ch, lcd_color_t fg, lcd_color_t bg, int scale)
{
    if (ch < 32 || ch > 127) ch = '?';
    if (scale < 1) scale = 1;
    if (scale > 2) scale = 2;
    const int w = 6 * scale;
    const int h = 8 * scale;
    uint8_t buf[12 * 16 * 2];
    size_t k = 0;
    const uint8_t *g = font5x7[(unsigned char)ch - 32];
    for (int py = 0; py < h; ++py) {
        int gy = py / scale;
        for (int px = 0; px < w; ++px) {
            int gx = px / scale;
            bool on = (gx < 5 && gy < 7) ? ((g[gx] >> gy) & 1U) : false;
            lcd_color_t c = on ? fg : bg;
            buf[k++] = (uint8_t)(c >> 8);
            buf[k++] = (uint8_t)c;
        }
    }
    if (x < 0 || y < 0 || x + w > LCD_H_RES || y + h > LCD_V_RES) return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    ESP_ERROR_CHECK(lcd_data(buf, k));
}

void lcd_draw_text(int x, int y, const char *text, lcd_color_t fg, lcd_color_t bg, int scale)
{
    if (!text) return;
    int cx = x;
    while (*text) {
        if (*text == '\n') {
            y += 8 * scale;
            cx = x;
        } else {
            lcd_draw_char(cx, y, *text, fg, bg, scale);
            cx += 6 * scale;
        }
        ++text;
    }
}

void lcd_draw_crosshair(int x, int y, lcd_color_t color)
{
    lcd_fill_rect(x - 12, y - 1, 25, 3, color);
    lcd_fill_rect(x - 1, y - 12, 3, 25, color);
    lcd_draw_rect(x - 7, y - 7, 15, 15, color, 1);
}
