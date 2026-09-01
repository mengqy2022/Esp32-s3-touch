#include "lv_port.h"

#include <string.h>

#include "board_pins.h"
#include "lcd_ili9341.h"
#include "xpt2046.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

// ui_process_cmd() lives in ui.c and must run on the LVGL task thread.
void ui_process_cmd(int type, int arg);

static const char *TAG = "lv_port";

#define LV_DISP_H_RES  LCD_H_RES
#define LV_DISP_V_RES  LCD_V_RES
#define LV_BUF_LINES   40

static lv_disp_draw_buf_t s_draw_buf;
DMA_ATTR static lv_color_t s_buf1[LV_DISP_H_RES * LV_BUF_LINES];
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static QueueHandle_t s_cmd_queue;

// ---------------- Display flush ----------------
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int x0 = area->x1, y0 = area->y1;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    lcd_draw_pixels(x0, y0, w, h, (const uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

// ---------------- Touch input ----------------
static void indev_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    static int16_t last_x, last_y;
    static bool last_pressed;

    int x = 0, y = 0;
    bool pressed = touch_read_xy(&x, &y);

    if (pressed) {
        if (x < 0 || x >= LV_DISP_H_RES || y < 0 || y >= LV_DISP_V_RES) {
            last_pressed = false;
        } else {
            last_x = (int16_t)x;
            last_y = (int16_t)y;
            last_pressed = true;
        }
    } else {
        last_pressed = false;
    }

    data->point.x = last_x;
    data->point.y = last_y;
    data->state = last_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---------------- Timer / task ----------------
static volatile bool s_suspended;

static void lvgl_task(void *arg)
{
    for (;;) {
        if (s_suspended) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        // Drain any UI commands first (must run in this LVGL thread).
        for (;;) {
            int cmd[2];
            if (xQueueReceive(s_cmd_queue, cmd, 0) != pdTRUE) break;
            ui_process_cmd(cmd[0], cmd[1]);
        }
        uint32_t tick = (uint32_t)(esp_timer_get_time() / 1000);
        lv_tick_inc(tick - lv_tick_get());
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void lv_port_post_cmd(int type, int arg)
{
    if (!s_cmd_queue) return;
    int cmd[2] = {type, arg};
    if (xQueueSend(s_cmd_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI command queue full; command %d dropped", type);
    }
}

void lv_port_suspend(void)
{
    s_suspended = true;
    vTaskDelay(pdMS_TO_TICKS(30));
}

void lv_port_resume(void)
{
    s_suspended = false;
}

esp_err_t lv_port_init(void)
{
    s_cmd_queue = xQueueCreate(24, sizeof(int[2]));
    if (!s_cmd_queue) return ESP_ERR_NO_MEM;

    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, LV_DISP_H_RES * LV_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LV_DISP_H_RES;
    s_disp_drv.ver_res = LV_DISP_V_RES;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = indev_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 10, NULL, 1);
    ESP_LOGI(TAG, "LVGL initialized: %dx%d", LV_DISP_H_RES, LV_DISP_V_RES);
    return ESP_OK;
}
