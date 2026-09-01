#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ax, bx, cx;
    float ay, by, cy;
    uint32_t magic;
} touch_calibration_t;

esp_err_t touch_init(void);
bool touch_is_pressed(void);
bool touch_read_raw(uint16_t *raw_x, uint16_t *raw_y);
bool touch_read_xy(int *x, int *y);
void touch_set_calibration(const touch_calibration_t *cal);
void touch_get_calibration(touch_calibration_t *cal);
bool touch_calibration_valid(void);
esp_err_t touch_load_calibration(void);
esp_err_t touch_save_calibration(const touch_calibration_t *cal);
void touch_clear_calibration(void);

// Solve a 3-point affine transform raw -> screen.
bool touch_solve_calibration(const uint16_t raw_x[3], const uint16_t raw_y[3],
                             const int screen_x[3], const int screen_y[3],
                             touch_calibration_t *out);

// Blocking 3-point calibration using the bare-metal LCD driver.
// Caller should pause LVGL input while this runs.
void touch_run_calibration(void);

#ifdef __cplusplus
}
#endif
