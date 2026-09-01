#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// UI screens
typedef enum {
    SCREEN_MENU = 0,     // main module menu
    SCREEN_SD,           // SD card checker
    SCREEN_SYSINFO,      // system information
    SCREEN_WIFI,         // wifi manager
    SCREEN_SUDOKU,       // sudoku game
    SCREEN_INVENTORY,    // component inventory (BOM in/out)
    SCREEN_VOCAB,        // vocabulary dashboard
    SCREEN_VOCAB_BOOKS,  // local + online wordbooks
    SCREEN_VOCAB_LEARN,  // flash-card learning
    SCREEN_VOCAB_DICTATION, // touchscreen dictation
    SCREEN_VOCAB_PLAN,   // daily target / progress
    SCREEN_2048,          // 2048 game
    SCREEN_FLAPPY,        // tap-to-fly pipe game
} ui_screen_t;

void ui_init(void);
void ui_draw_all(void);          // (re)build the current screen's LVGL widgets
void ui_update_status(void);     // refresh dynamic content of current screen
void ui_set_screen(ui_screen_t scr);
ui_screen_t ui_get_screen(void);
void ui_start_timer(void);       // start the periodic LVGL refresh timer

// Executed on the LVGL task thread (called by lv_port). Do NOT call directly.
void ui_process_cmd(int type, int arg);

void ui_set_scanning(bool scanning);
bool ui_get_scanning(void);
void ui_set_auto(bool enabled);
bool ui_get_auto(void);

// Called periodically (from the main loop) to refresh dynamic content.
void ui_tick(void);

// WIFI helpers (used by wifi_mgr callbacks / main)
void ui_wifi_state_changed(void);   // rebuild wifi screen widgets when state changes
const char *ui_wifi_get_pass(void);
int  ui_wifi_get_selected(void);
void ui_wifi_select_ap(int idx);

#ifdef __cplusplus
}
#endif
