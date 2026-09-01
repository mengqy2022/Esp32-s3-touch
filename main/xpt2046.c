#include "xpt2046.h"
#include "board_pins.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "nvs.h"

#define TOUCH_CAL_MAGIC 0x54434153UL // 'TCAS' - bump to force recalibration for LVGL touch mapping
static const char *TAG = "touch";
static touch_calibration_t s_cal;
static bool s_cal_valid;

static uint8_t bb_xfer8(uint8_t out)
{
    uint8_t in = 0;
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(PIN_TP_MOSI, (out >> bit) & 1U);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_TP_SCK, 1);
        in = (uint8_t)((in << 1) | (gpio_get_level(PIN_TP_MISO) ? 1 : 0));
        esp_rom_delay_us(1);
        gpio_set_level(PIN_TP_SCK, 0);
    }
    return in;
}

static uint16_t read_adc12(uint8_t command)
{
    gpio_set_level(PIN_TP_CS, 0);
    bb_xfer8(command);
    uint8_t hi = bb_xfer8(0x00);
    uint8_t lo = bb_xfer8(0x00);
    gpio_set_level(PIN_TP_CS, 1);
    return (uint16_t)((((uint16_t)hi << 8) | lo) >> 3) & 0x0FFF;
}

esp_err_t touch_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_TP_SCK) | (1ULL << PIN_TP_MOSI) | (1ULL << PIN_TP_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_TP_SCK, 0);
    gpio_set_level(PIN_TP_MOSI, 0);
    gpio_set_level(PIN_TP_CS, 1);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_TP_MISO) | (1ULL << PIN_TP_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // IRQ has external 10k pull-up on the board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
    return ESP_OK;
}

bool touch_is_pressed(void)
{
    return gpio_get_level(PIN_TP_IRQ) == 0;
}

static uint16_t median5(uint16_t v[5])
{
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            if (v[j] < v[i]) {
                uint16_t t = v[i]; v[i] = v[j]; v[j] = t;
            }
        }
    }
    return v[2];
}

bool touch_read_raw(uint16_t *raw_x, uint16_t *raw_y)
{
    if (!touch_is_pressed()) return false;
    uint16_t xs[5], ys[5];
    for (int i = 0; i < 5; ++i) {
        // XPT2046 commands: X=0xD0, Y=0x90, 12-bit differential measurement.
        xs[i] = read_adc12(0xD0);
        ys[i] = read_adc12(0x90);
    }
    if (raw_x) *raw_x = median5(xs);
    if (raw_y) *raw_y = median5(ys);
    return true;
}

bool touch_read_xy(int *x, int *y)
{
    if (!s_cal_valid) return false;
    uint16_t rx, ry;
    if (!touch_read_raw(&rx, &ry)) return false;
    int sx = (int)lroundf(s_cal.ax * rx + s_cal.bx * ry + s_cal.cx);
    int sy = (int)lroundf(s_cal.ay * rx + s_cal.by * ry + s_cal.cy);
    if (sx < 0) sx = 0;
    if (sx >= LCD_H_RES) sx = LCD_H_RES - 1;
    if (sy < 0) sy = 0;
    if (sy >= LCD_V_RES) sy = LCD_V_RES - 1;
    if (x) *x = sx;
    if (y) *y = sy;
    return true;
}

void touch_set_calibration(const touch_calibration_t *cal)
{
    if (!cal) return;
    s_cal = *cal;
    s_cal.magic = TOUCH_CAL_MAGIC;
    s_cal_valid = true;
}

void touch_get_calibration(touch_calibration_t *cal)
{
    if (cal) *cal = s_cal;
}

bool touch_calibration_valid(void)
{
    return s_cal_valid;
}

esp_err_t touch_load_calibration(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("touch", NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = sizeof(s_cal);
    err = nvs_get_blob(h, "affine", &s_cal, &len);
    nvs_close(h);
    if (err == ESP_OK && len == sizeof(s_cal) && s_cal.magic == TOUCH_CAL_MAGIC) {
        s_cal_valid = true;
        ESP_LOGI(TAG, "touch calibration loaded");
        return ESP_OK;
    }
    s_cal_valid = false;
    return err == ESP_OK ? ESP_ERR_INVALID_CRC : err;
}

esp_err_t touch_save_calibration(const touch_calibration_t *cal)
{
    if (!cal) return ESP_ERR_INVALID_ARG;
    touch_calibration_t tmp = *cal;
    tmp.magic = TOUCH_CAL_MAGIC;
    nvs_handle_t h;
    esp_err_t err = nvs_open("touch", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "affine", &tmp, sizeof(tmp));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) touch_set_calibration(&tmp);
    return err;
}

void touch_clear_calibration(void)
{
    s_cal_valid = false;
    memset(&s_cal, 0, sizeof(s_cal));
    nvs_handle_t h;
    if (nvs_open("touch", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "affine");
        nvs_commit(h);
        nvs_close(h);
    }
}

bool touch_solve_calibration(const uint16_t raw_x[3], const uint16_t raw_y[3],
                             const int screen_x[3], const int screen_y[3],
                             touch_calibration_t *out)
{
    if (!raw_x || !raw_y || !screen_x || !screen_y || !out) return false;
    double x1=raw_x[0], y1=raw_y[0], x2=raw_x[1], y2=raw_y[1], x3=raw_x[2], y3=raw_y[2];
    double d = x1*(y2-y3) + x2*(y3-y1) + x3*(y1-y2);
    if (fabs(d) < 1.0) return false;

    double sx1=screen_x[0], sx2=screen_x[1], sx3=screen_x[2];
    double sy1=screen_y[0], sy2=screen_y[1], sy3=screen_y[2];
    out->ax = (float)((sx1*(y2-y3) + sx2*(y3-y1) + sx3*(y1-y2)) / d);
    out->bx = (float)((sx1*(x3-x2) + sx2*(x1-x3) + sx3*(x2-x1)) / d);
    out->cx = (float)((sx1*(x2*y3-x3*y2) + sx2*(x3*y1-x1*y3) + sx3*(x1*y2-x2*y1)) / d);

    out->ay = (float)((sy1*(y2-y3) + sy2*(y3-y1) + sy3*(y1-y2)) / d);
    out->by = (float)((sy1*(x3-x2) + sy2*(x1-x3) + sy3*(x2-x1)) / d);
    out->cy = (float)((sy1*(x2*y3-x3*y2) + sy2*(x3*y1-x1*y3) + sy3*(x1*y2-x2*y1)) / d);
    out->magic = TOUCH_CAL_MAGIC;
    return true;
}

// ---------------- Blocking calibration (bare-metal LCD) ----------------
#include "lcd_ili9341.h"
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bool wait_touch_average(uint16_t *rx, uint16_t *ry)
{
    while (!touch_is_pressed()) vTaskDelay(pdMS_TO_TICKS(20));
    vTaskDelay(pdMS_TO_TICKS(60));
    uint32_t sx = 0, sy = 0;
    int good = 0;
    for (int i = 0; i < 12; ++i) {
        uint16_t x, y;
        if (touch_read_raw(&x, &y)) {
            sx += x;
            sy += y;
            good++;
        }
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    while (touch_is_pressed()) vTaskDelay(pdMS_TO_TICKS(20));
    vTaskDelay(pdMS_TO_TICKS(120));
    if (good < 5) return false;
    *rx = (uint16_t)(sx / good);
    *ry = (uint16_t)(sy / good);
    return true;
}

void touch_run_calibration(void)
{
    const int sx[3] = {30, 290, 30};
    const int sy[3] = {30, 30, 210};
    uint16_t rx[3] = {0}, ry[3] = {0};

    for (;;) {
        lcd_fill_screen(LCD_DARK);
        lcd_draw_text(40, 104, "TOUCH CALIBRATION", LCD_WHITE, LCD_DARK, 2);
        lcd_draw_text(67, 128, "TOUCH 3 CROSSES", LCD_CYAN, LCD_DARK, 1);
        vTaskDelay(pdMS_TO_TICKS(700));

        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            lcd_fill_screen(LCD_DARK);
            char step[24];
            snprintf(step, sizeof(step), "POINT %d / 3", i + 1);
            lcd_draw_text(116, 108, step, LCD_WHITE, LCD_DARK, 1);
            lcd_draw_crosshair(sx[i], sy[i], LCD_YELLOW);
            if (!wait_touch_average(&rx[i], &ry[i])) { ok = false; break; }
        }

        touch_calibration_t cal;
        if (ok && touch_solve_calibration(rx, ry, sx, sy, &cal)) {
            if (touch_save_calibration(&cal) == ESP_OK) {
                lcd_fill_screen(LCD_DARK);
                lcd_draw_text(82, 105, "CALIBRATION OK", LCD_GREEN, LCD_DARK, 2);
                vTaskDelay(pdMS_TO_TICKS(900));
                return;
            }
        }
        lcd_fill_screen(LCD_DARK);
        lcd_draw_text(70, 105, "CAL FAILED - RETRY", LCD_RED, LCD_DARK, 2);
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
}
