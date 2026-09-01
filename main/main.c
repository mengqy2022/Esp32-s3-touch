#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board_pins.h"
#include "file_xfer.h"
#include "lcd_ili9341.h"
#include "lv_port.h"
#include "net_utils.h"
#include "sd_monitor.h"
#include "ui.h"
#include "wifi_mgr.h"
#include "vocabulary.h"
#include "xpt2046.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "app";

#define AUTO_PERIOD_US 3000000LL

static void rgb_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LED_R) | (1ULL << PIN_LED_G) | (1ULL << PIN_LED_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_LED_R, 1);
    gpio_set_level(PIN_LED_G, 1);
    gpio_set_level(PIN_LED_B, 1);
}

static void rgb_set(bool r, bool g, bool b)
{
    // Common-anode LED: LOW means ON.
    gpio_set_level(PIN_LED_R, r ? 0 : 1);
    gpio_set_level(PIN_LED_G, g ? 0 : 1);
    gpio_set_level(PIN_LED_B, b ? 0 : 1);
}

// SD probing runs in its own task so the UI stays responsive while scanning.
static void scan_task(void *arg)
{
    sd_monitor_probe();

    const sdmon_status_t *s = sd_monitor_get_status();
    if (s->state == SDMON_READ_OK || s->state == SDMON_EMPTY_OK) {
        rgb_set(false, true, false); // green on success
    } else {
        rgb_set(true, false, false); // red on failure
    }

    ui_set_scanning(false);
    ui_update_status();
    vTaskDelete(NULL);
}

void sd_scan_request(void)
{
    if (ui_get_scanning()) return; // a scan is already running

    ui_set_scanning(true);
    ui_update_status();
    rgb_set(false, false, true); // blue while scanning

    // 8192 bytes of stack for FAT mount + directory walk.
    if (xTaskCreate(scan_task, "sd_scan", 8192, NULL, 5, NULL) != pdPASS) {
        ui_set_scanning(false);
        rgb_set(true, false, false);
    }
}

// BOOT button (GPIO0, active low): long-press ~2s triggers touch recalibration.
#define BOOT_BTN_GPIO     GPIO_NUM_0
#define BOOT_HOLD_MS      2000
#define BOOT_REPEAT_MS    3000 // after first trigger, require release+repress

static void boot_btn_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOOT_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // board has pull-up; enable anyway
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    bool pressed = false;
    int64_t press_start_us = 0;
    bool triggered = false;

    for (;;) {
        bool now = (gpio_get_level(BOOT_BTN_GPIO) == 0);
        if (now && !pressed) {
            pressed = true;
            triggered = false;
            press_start_us = esp_timer_get_time();
        } else if (!now && pressed) {
            pressed = false;
        } else if (now && pressed && !triggered) {
            int64_t held = esp_timer_get_time() - press_start_us;
            if (held >= BOOT_HOLD_MS * 1000LL) {
                triggered = true;
                ESP_LOGI(TAG, "BOOT long-press: recalibrating touch...");
                rgb_set(true, false, true); // magenta
                // Suspend LVGL so the bare-metal LCD driver owns the SPI bus.
                lv_port_suspend();
                touch_clear_calibration();
                touch_run_calibration();
                lv_port_resume();
                rgb_set(false, false, true);
                ui_set_screen(SCREEN_MENU);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// WiFi connection completion: kick off SNTP + geolocation once connected.
static void on_wifi_got_ip(void)
{
    ESP_LOGI(TAG, "WiFi connected, starting SNTP + geolocation");
    net_sntp_start();
    net_loc_start_query();
}

static void wifi_event_hook(void)
{
    static bool s_was_connected;
    bool connected = wifi_mgr_is_connected();
    if (connected && !s_was_connected) {
        on_wifi_got_ip();
    }
    s_was_connected = connected;
}

// Background task: watches WiFi state and triggers SNTP/loc when IP arrives.
static void wifi_watch_task(void *arg)
{
    for (;;) {
        wifi_event_hook();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    rgb_init();
    rgb_set(false, false, true);
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(touch_init());
    ESP_ERROR_CHECK(sd_monitor_init());
    wifi_mgr_init();
    net_time_init();
    ESP_ERROR_CHECK(vocab_init());

    // First boot (or after orientation change): calibrate touch BEFORE
    // LVGL starts, so the bare-metal LCD driver owns the SPI bus exclusively.
    if (touch_load_calibration() != ESP_OK || !touch_calibration_valid()) {
        touch_run_calibration();
    }

    ESP_ERROR_CHECK(lv_port_init());
    ui_init();
    ui_start_timer();

    sd_scan_request();
    // Auto-reconnect is handled by the WIFI_EVENT_STA_START handler in wifi_mgr.
    file_xfer_start(); // PC serial file-transfer service (INVENTORY.CSV etc.)

    xTaskCreate(wifi_watch_task, "wifi_watch", 3072, NULL, 3, NULL);
    xTaskCreate(boot_btn_task, "boot_btn", 4096, NULL, 3, NULL);

    // AUTO mode timer: trigger SD scans while on the SD screen with AUTO on.
    int64_t last_auto_us = esp_timer_get_time();
    while (1) {
        if (ui_get_screen() == SCREEN_SD && ui_get_auto()) {
            int64_t now = esp_timer_get_time();
            if (now - last_auto_us >= AUTO_PERIOD_US) {
                sd_scan_request();
                last_auto_us = now;
            }
        }
        ui_tick();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
