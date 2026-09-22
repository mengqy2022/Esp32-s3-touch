#ifndef PC_MONITOR_V6_LVGL_DYNAMIC_H
#define PC_MONITOR_V6_LVGL_DYNAMIC_H

/*
 PC Monitor v6.0 LVGL Dynamic Gauge Edition

 UI:
 - LVGL Arc gauges
 - Animated pointer values
 - Real time charts
 - Multi page dashboard

 Pages:
 1. Main Dashboard
 2. CPU / GPU Detail
 3. System Sensors

 Refresh:
 1000ms sensor update
 30-60 FPS UI refresh target
*/

#define PCMON_V6_SENSOR_PERIOD_MS 1000
#define PCMON_V6_UI_REFRESH_MS 30
#define PCMON_V6_HISTORY_SIZE 120

typedef enum {
    PCMON_PAGE_MAIN = 0,
    PCMON_PAGE_DETAIL,
    PCMON_PAGE_SYSTEM
} pcmon_page_t;

typedef struct {
    int cpu_usage;
    int gpu_usage;
    int mem_usage;
    int cpu_temp;
    int gpu_temp;
    int rx_kbps;
    int tx_kbps;
} pcmon_sensor_t;

#endif
