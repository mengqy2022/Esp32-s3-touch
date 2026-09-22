#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// UI commands posted to the LVGL task (thread-safe via FreeRTOS queue).
typedef enum {
    UI_CMD_SCREEN = 0,   // arg: ui_screen_t
    UI_CMD_DRAW,         // arg: unused
    UI_CMD_STATUS,       // arg: unused
    UI_CMD_START_TIMER,  // arg: unused
} ui_cmd_type_t;

// Initializes LVGL, the display driver (backs onto lcd_ili9341.c)
// and the touch input driver (backs onto xpt2046.c).
esp_err_t lv_port_init(void);

// Suspend / resume the LVGL task. Use around bare-metal LCD work
// (e.g. touch calibration) to avoid SPI bus contention.
void lv_port_suspend(void);
void lv_port_resume(void);

// Post a UI command; executed on the LVGL task thread. Thread-safe.
void lv_port_post_cmd(int type, int arg);

// Milliseconds (esp_timer based) of the last touch press/release. Used by the
// idle "return to clock" logic in main.c. Never decreases (boots at 0).
uint32_t lv_port_last_input_ms(void);

#ifdef __cplusplus
}
#endif
