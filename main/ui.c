#include "ui.h"
#include "inventory.h"
#include "lcd_ili9341.h"
#include "lv_port.h"
#include "net_utils.h"
#include "pc_monitor.h"
#include "sd_monitor.h"
#include "sudoku.h"
#include "game2048.h"
#include "flappy.h"
#include "wifi_mgr.h"
#include "vocabulary.h"
#include "xpt2046.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

// Uncompressed CJK font (GB2312 level-1) for Chinese SSIDs etc.
extern const lv_font_t font_cn16;

static const char *TAG = "ui";

#define AUTO_PERIOD_US 3000000LL

// Defined in main.c
void sd_scan_request(void);

// ---------------- Theme (minimal graphite / high-contrast touch UI) ----------------
#define CLR_BG        lv_color_hex(0x0B0F14)
#define CLR_PANEL     lv_color_hex(0x151B23)
#define CLR_PANEL_2   lv_color_hex(0x1D2632)
#define CLR_PRIMARY   lv_color_hex(0x3B82F6)
#define CLR_PRIMARY_D lv_color_hex(0x2563EB)
#define CLR_ACCENT    lv_color_hex(0x22C55E)
#define CLR_TEXT      lv_color_hex(0xF8FAFC)
#define CLR_TEXT_DIM  lv_color_hex(0x94A3B8)
#define CLR_OK        lv_color_hex(0x22C55E)
#define CLR_ERR       lv_color_hex(0xEF4444)
#define CLR_WARN      lv_color_hex(0xF59E0B)

// ---------------- static state ----------------
static ui_screen_t s_screen = SCREEN_MENU;
static bool s_auto;
static bool s_scanning;
static int64_t s_last_scan_us;

static lv_obj_t *s_sd_banner;
static lv_obj_t *s_sd_events;
static lv_obj_t *s_sd_files;
static lv_obj_t *s_sd_cap;
static lv_obj_t *s_sd_sample;
static lv_obj_t *s_sd_bytes;
static lv_obj_t *s_sd_auto;

static lv_obj_t *s_sys_uptime;
static lv_obj_t *s_sys_time;
static lv_obj_t *s_sys_wifi;
static lv_obj_t *s_sys_loc;

static lv_obj_t *s_wifi_status;
static lv_obj_t *s_wifi_list;
static lv_obj_t *s_wifi_ta;
static lv_obj_t *s_wifi_kb;
static int s_wifi_sel = -1;
static wifi_state_t s_last_wifi_state = (wifi_state_t)-1;

// MENU bottom info labels
static lv_obj_t *s_menu_time;
static lv_obj_t *s_menu_loc;

// Touch-calibration confirmation overlay (LVGL top layer)
static lv_obj_t *s_cal_layer;

// PC MONITOR screen widgets
static lv_obj_t *s_pcmon_clock;
static lv_obj_t *s_pcmon_date;
static lv_obj_t *s_pcmon_link;
static lv_obj_t *s_pcmon_cpu_bar;
static lv_obj_t *s_pcmon_cpu_val;
static lv_obj_t *s_pcmon_gpu_bar;
static lv_obj_t *s_pcmon_gpu_val;
static lv_obj_t *s_pcmon_mem_bar;
static lv_obj_t *s_pcmon_mem_val;
static lv_obj_t *s_pcmon_mem_detail;
static lv_obj_t *s_pcmon_status;

// SUDOKU state
static sudoku_t s_sudoku;
static game2048_t s_game2048;
static lv_obj_t *s_2048_cells[4][4];
static lv_obj_t *s_2048_score;
static lv_obj_t *s_2048_status;
static lv_point_t s_2048_touch_start;
static bool s_2048_touch_active;

// FLAPPY state
static flappy_game_t s_flappy;
static lv_obj_t *s_flappy_board;
static lv_obj_t *s_flappy_bird;
static lv_obj_t *s_flappy_pipe_top[FLAPPY_PIPE_COUNT];
static lv_obj_t *s_flappy_pipe_bottom[FLAPPY_PIPE_COUNT];
static lv_obj_t *s_flappy_score;
static lv_obj_t *s_flappy_status;
static lv_timer_t *s_flappy_timer;
static int s_sudoku_puzzle;
static lv_obj_t *s_sudo_cells[9][9];
static lv_obj_t *s_sudo_msg;

// INVENTORY state
#define INV_ROW_POOL  8
#define INV_ROW_H     21

typedef struct {
    lv_obj_t *row;
    lv_obj_t *model;
    lv_obj_t *qty;
    lv_obj_t *product_no;
    lv_obj_t *minus;
    lv_obj_t *plus;
    lv_obj_t *del;
} inv_row_ui_t;

static inv_db_t s_inv;
static lv_obj_t *s_inv_list;
static lv_obj_t *s_inv_stats;
static lv_obj_t *s_inv_extent;
static lv_obj_t *s_inv_filter_btn[4];
static inv_row_ui_t s_inv_rows[INV_ROW_POOL];
static int s_inv_view_indices[INV_MAX_ITEMS];
static int s_inv_view_count;
static int s_inv_first_row;
static inv_hist_t s_inv_hist_cache[INV_HIST_MAX];
static int s_inv_hist_count;
static int s_inv_filter;        // 0=ALL, 1=IN(stock), 2=OUT(stock), 3=history
static char s_inv_query[40];    // search filter for MODEL/PRODUCT_NO
static int s_inv_edit_idx;      // item index being quantity-edited (-1 none)
static lv_obj_t *s_inv_edit_overlay;

// VOCAB state
static vocab_book_info_t s_vocab_local[VOCAB_LOCAL_MAX];
static int s_vocab_local_count;
static lv_obj_t *s_vocab_status;
static lv_obj_t *s_vocab_dl_label;
static char s_vocab_books_notice[112];
static lv_obj_t *s_vocab_word;
static lv_obj_t *s_vocab_phonetic;
static lv_obj_t *s_vocab_meaning;
static lv_obj_t *s_vocab_progress;
static lv_obj_t *s_vocab_feedback;
static bool s_vocab_revealed;
static lv_obj_t *s_dict_prompt;
static lv_obj_t *s_dict_ta;
static lv_obj_t *s_dict_kb;
static lv_obj_t *s_dict_feedback;
static lv_obj_t *s_dict_check;
static bool s_dict_checked;

#define HAND_W 300
#define HAND_H 72
#define HAND_MAX_POINTS 640
#define HAND_GRID_W 9
#define HAND_GRID_H 13
#define HAND_MAX_SEGMENTS 12
#define HAND_SEG_GAP 5
#define HAND_BREAK_COORD (-32768)
static lv_obj_t *s_hand_canvas;
static lv_obj_t *s_hand_mode_btn;
static lv_obj_t *s_hand_hint;
static lv_obj_t *s_hand_preview;
static lv_obj_t *s_hand_add;
static lv_obj_t *s_hand_clear;
static lv_obj_t *s_hand_del;
/* Handwriting is rendered directly in the pad's draw event.
 * This avoids indexed-canvas palette issues and keeps RAM usage low. */
static lv_point_t s_hand_points[HAND_MAX_POINTS];
static int s_hand_point_count;
static bool s_hand_pen_down;
static bool s_dict_hand_mode = true;
static lv_obj_t *s_plan_value;
static lv_obj_t *s_plan_stats;
static uint16_t s_plan_edit_target;
static char s_plan_notice[64];
static bool s_vocab_reset_armed;
static vocab_download_state_t s_last_dl_state = (vocab_download_state_t)-1;
static char s_vocab_home_notice[96];

static uint32_t s_last_click_ms;    // short touch de-bounce between accepted taps
static uint32_t s_screen_enter_ms;   // guard against the release event that opened a screen

static lv_timer_t *s_ui_timer;

// ---------------- forward decls ----------------
static void build_menu_screen(void);
static void build_sd_screen(void);
static void build_sysinfo_screen(void);
static void build_wifi_screen(void);
static void refresh_sd_labels(void);
static void refresh_sysinfo_labels(void);
static void refresh_wifi_labels(void);
static void ui_timer_cb(lv_timer_t *t);
static void on_back(lv_event_t *e);
const char *sd_state_text(sdmon_state_t st);
static lv_color_t state_color(sdmon_state_t st);
static void refresh_menu_bottom(void);
static void build_sudoku_screen(void);
static void build_2048_screen(void);
static void refresh_2048(void);
static void build_flappy_screen(void);
static void refresh_flappy(void);
static void flappy_timer_cb(lv_timer_t *t);
static void refresh_sudoku_board(void);
static void build_inventory_screen(void);
static void refresh_inventory_list(void);
static void refresh_inventory_window(bool force);
static void on_inv_scroll(lv_event_t *e);
static void on_inv_bg_click(lv_event_t *e);
static void build_vocab_screen(void);
static void build_vocab_books_screen(void);
static void build_vocab_learn_screen(void);
static void build_vocab_dictation_screen(void);
static void build_vocab_plan_screen(void);
static void refresh_vocab_dashboard(void);
static void refresh_vocab_learn_card(void);
static void refresh_vocab_dictation(void);
static void refresh_vocab_download_label(void);
static void refresh_plan_labels(void);
static void poll_vocab_open_status(void);
static void hand_clear_pad(void);
static void hand_draw_event(lv_event_t *e);
static void hand_clear_event(lv_event_t *e);
static void hand_add_event(lv_event_t *e);
static void hand_del_event(lv_event_t *e);
static void dict_check_event(lv_event_t *e);
static int hand_recognize_segments(char *out, size_t out_sz, int *avg_confidence);
static int hand_commit_pad(bool show_feedback);
static void plan_action_event(lv_event_t *e);
static void show_cal_confirm(void);
static void build_pcmon_screen(void);
static void refresh_pcmon_screen(void);
static void on_pcmon_tap(lv_event_t *e);

// ---------------- helpers ----------------
static void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_btn(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, CLR_PRIMARY, 0);
    lv_obj_set_style_bg_color(btn, CLR_PRIMARY_D, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x334155), 0);
    lv_obj_set_style_text_color(btn, CLR_TEXT, 0);
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, int16_t user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_user_data(btn, (void *)(intptr_t)user_data);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    // Buttons must only respond to taps — no scroll/drag feedback.
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    style_btn(btn);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_color(label, CLR_TEXT, 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_color(lab, color, 0);
    lv_obj_set_pos(lab, x, y);
    return lab;
}

static void label_set_text_if_changed(lv_obj_t *lab, const char *txt)
{
    if (!lab || !txt) return;
    const char *cur = lv_label_get_text(lab);
    if (cur && strcmp(cur, txt) == 0) return;
    lv_label_set_text(lab, txt);
}

static lv_obj_t *make_title(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, txt);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(title, 220, 22);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, CLR_TEXT, 0);
    lv_obj_set_pos(title, 14, 9);
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, 48, 3);
    lv_obj_set_style_bg_color(line, CLR_PRIMARY, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 2, 0);
    lv_obj_set_pos(line, 14, 32);
    return title;
}

static lv_obj_t *make_back_btn(lv_obj_t *parent)
{
    lv_obj_t *back = make_btn(parent, "BACK", on_back, 0);
    lv_obj_set_size(back, 58, 28);
    lv_obj_set_style_bg_color(back, CLR_PANEL, 0);
    lv_obj_set_style_bg_color(back, CLR_ERR, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(back, CLR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -8, 6);
    return back;
}

// ---------------- common actions ----------------
// Event callbacks run on the LVGL thread. Screen switches still go through
// the command queue to guarantee ordering and to defer widget deletion
// until after the triggering event has been fully processed.

// Keep screen-transition protection separate from normal tap de-bounce.
#define UI_SCREEN_GUARD_MS  180U
#define UI_TAP_DEBOUNCE_MS   80U

static bool click_ok(void)
{
    uint32_t now = lv_tick_get();
    if (now - s_screen_enter_ms < UI_SCREEN_GUARD_MS) return false;
    if (now - s_last_click_ms < UI_TAP_DEBOUNCE_MS) return false;
    s_last_click_ms = now;
    return true;
}

// ---------------- CALIBRATE confirmation dialog ----------------
// One-tap "CALIBRATE" used to erase the touch mapping and force a 3-point
// recalibration with no way back - far too easy to trigger by accident. The
// menu button now opens this confirmation dialog first.

static void on_cal_scrim_click(lv_event_t *e)
{
    (void)e; // swallow taps on the dimmed background
}

static void on_cal_cancel(lv_event_t *e)
{
    (void)e;
    if (!click_ok()) return;
    if (s_cal_layer) {
        lv_obj_del(s_cal_layer);
        s_cal_layer = NULL;
    }
}

static void on_cal_confirm(lv_event_t *e)
{
    (void)e;
    if (!click_ok()) return;
    if (s_cal_layer) {
        lv_obj_del(s_cal_layer);
        s_cal_layer = NULL;
    }
    // Re-run touch calibration: suspend LVGL first so the bare-metal
    // LCD driver owns the SPI bus exclusively. Same flow as the old
    // unconditional menu action - now only after explicit confirmation.
    lv_port_suspend();
    touch_clear_calibration();
    touch_run_calibration();
    lv_port_resume();
    lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_MENU);
}

static void show_cal_confirm(void)
{
    if (s_cal_layer) return; // already open

    lv_obj_t *layer = lv_obj_create(lv_layer_top());
    s_cal_layer = layer;
    lv_obj_set_size(layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_style_bg_color(layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_60, 0);
    lv_obj_set_style_border_width(layer, 0, 0);
    lv_obj_set_style_radius(layer, 0, 0);
    lv_obj_set_style_pad_all(layer, 0, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(layer, on_cal_scrim_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(layer);
    lv_obj_set_size(card, 280, 118);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, CLR_PANEL, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, CLR_TEXT_DIM, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "TOUCH CALIBRATE?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, CLR_WARN, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *body = lv_label_create(card);
    lv_label_set_text(body, "Start 3-point calibration?\nCurrent calibration will be lost.");
    lv_obj_set_style_text_color(body, CLR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *ok = make_btn(card, "CONFIRM", on_cal_confirm, 0);
    lv_obj_set_size(ok, 110, 32);
    lv_obj_set_style_bg_color(ok, CLR_WARN, 0);
    lv_obj_set_style_bg_color(ok, CLR_PRIMARY_D, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(ok, 0), lv_color_black(), 0);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_LEFT, 14, -10);

    lv_obj_t *no = make_btn(card, "CANCEL", on_cal_cancel, 0);
    lv_obj_set_size(no, 110, 32);
    lv_obj_set_style_bg_color(no, CLR_PANEL_2, 0);
    lv_obj_align(no, LV_ALIGN_BOTTOM_RIGHT, -14, -10);
}

static void on_menu_btn(lv_event_t *e)
{
    // IMPORTANT: lv_event_get_user_data() returns the data registered with
    // the event callback (NULL here), NOT the widget's user_data. Must go
    // through the target widget.
    lv_obj_t *target = lv_event_get_target(e);
    int16_t id = (int16_t)(intptr_t)lv_obj_get_user_data(target);
    if (!click_ok()) return;
    switch (id) {
        case 0: lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_SD); break;
        case 1: lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_SYSINFO); break;
        case 2: lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_WIFI); break;
        case 3: {
            // Ask for confirmation before wiping the touch calibration.
            show_cal_confirm();
            break;
        }
        case 4: {
            // New sudoku puzzle on entry.
            s_sudoku_puzzle = (s_sudoku_puzzle + 1) % SUDOKU_PUZZLES;
            sudoku_new(&s_sudoku, s_sudoku_puzzle);
            lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_SUDOKU);
            break;
        }
        case 7:
            game2048_init(&s_game2048);
            lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_2048);
            break;
        case 8:
            flappy_init(&s_flappy);
            lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_FLAPPY);
            break;
        case 5: {
            // Enter inventory: ensure folders + load stock from SD card.
            inv_ensure_folders();
            inv_load(&s_inv);
            s_inv_filter = 0;
            s_inv_query[0] = '\0';
            s_inv_edit_idx = -1;
            lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_INVENTORY);
            break;
        }
        case 6:
            lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_VOCAB);
            break;
        case 9:
            lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_PCMON);
            break;
        default: break;
    }
}

static void on_back(lv_event_t *e)
{
    if (!click_ok()) return;
    if (s_screen == SCREEN_WIFI && s_wifi_sel >= 0) {
        // Back from password view to AP list view.
        s_wifi_sel = -1;
        lv_port_post_cmd(UI_CMD_DRAW, 0);
        return;
    }
    if (s_screen == SCREEN_VOCAB_BOOKS || s_screen == SCREEN_VOCAB_LEARN ||
        s_screen == SCREEN_VOCAB_DICTATION || s_screen == SCREEN_VOCAB_PLAN) {
        lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_VOCAB);
        return;
    }
    lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_MENU);
}

static void on_sd_scan(lv_event_t *e)
{
    if (!click_ok()) return;
    sd_scan_request();
}

static void on_sd_auto(lv_event_t *e)
{
    if (!click_ok()) return;
    ui_set_auto(!ui_get_auto());
    lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_SD); // rebuild to refresh AUTO button state
    s_last_scan_us = esp_timer_get_time();
}

static void on_wifi_scan(lv_event_t *e)
{
    if (!click_ok()) return;
    wifi_mgr_scan_async();
}

static void on_wifi_connect(lv_event_t *e)
{
    if (!click_ok()) return;
    if (s_wifi_sel < 0) return;
    wifi_ap_t ap;
    if (!wifi_mgr_get_ap(s_wifi_sel, &ap)) return;
    const char *pass = lv_textarea_get_text(s_wifi_ta);
    wifi_mgr_connect(ap.ssid, pass);
    // Stay on view 2; when the state flips to CONNECTED we return to the list.
}

static void on_wifi_disconnect(lv_event_t *e)
{
    if (!click_ok()) return;
    wifi_mgr_disconnect();
    s_wifi_sel = -1;
    lv_port_post_cmd(UI_CMD_DRAW, 0); // back to view 1 list
}

static void on_ap_click(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    if (idx < 0) return;
    s_wifi_sel = idx;
    // Rebuild the CURRENT screen (view 2) WITHOUT resetting s_wifi_sel.
    // (UI_CMD_SCREEN goes through do_set_screen which clears the selection.)
    lv_port_post_cmd(UI_CMD_DRAW, 0);
}

// ---------------- MENU screen ----------------
// The module buttons live in a vertically scrollable container so the layout
// stays clean even with many modules (up to 10+). The status card is pinned
// at the bottom and never overlaps the buttons.
static void build_menu_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "ESP32 Monitor");

    // Scrollable module list container.
    lv_obj_t *sc = lv_obj_create(scr);
    lv_obj_set_size(sc, 300, 130);
    lv_obj_set_pos(sc, 10, 38);
    lv_obj_set_style_bg_color(sc, CLR_BG, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_set_style_radius(sc, 0, 0);
    lv_obj_set_style_pad_all(sc, 0, 0);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);

    struct { const char *label; int16_t id; lv_color_t color; } cards[] = {
        { "SD CHECK",   0, lv_color_hex(0x2E86DE) },
        { "SYSTEM",     1, lv_color_hex(0x8E6CE0) },
        { "WIFI",       2, lv_color_hex(0x00C9A7) },
        { "PC MONITOR", 9, lv_color_hex(0x0EA5E9) },
        { "CALIBRATE",  3, lv_color_hex(0xF5A623) },
        { "SUDOKU",     4, lv_color_hex(0xE0556D) },
        { "2048",       7, lv_color_hex(0xF97316) },
        { "FLAPPY",     8, lv_color_hex(0x38BDF8) },
        { "INVENTORY",  5, lv_color_hex(0x4CAF50) },
        { "VOCAB LAB",  6, lv_color_hex(0x7C3AED) },
        // Extensible: add more modules below; the container scrolls.
        // { "BLUETOOTH", 6, ... },
    };
    const int n = (int)(sizeof(cards) / sizeof(cards[0]));

    // 2 columns; each button 140x36 with 8px row gap. With 10 modules this is
    // 5 rows * 44px = 220px > 130px container height -> scrolls, no overlap.
    const lv_coord_t btn_w = 140, btn_h = 42, gap = 8;
    for (int i = 0; i < n; ++i) {
        lv_obj_t *btn = make_btn(sc, cards[i].label, on_menu_btn, cards[i].id);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_style_bg_color(btn, cards[i].color, 0);
        lv_obj_set_style_bg_color(btn, CLR_PRIMARY_D, LV_STATE_PRESSED);
        lv_coord_t x = (i % 2) ? (btn_w + 6) : 0;
        lv_coord_t y = (i / 2) * (btn_h + gap);
        lv_obj_set_pos(btn, x, y);
    }

    // Bottom status card (pinned, fixed height, separated from the menu list).
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 300, 64);
    lv_obj_set_pos(card, 10, 172);
    lv_obj_set_style_bg_color(card, CLR_PANEL, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);

    s_menu_time = lv_label_create(card);
    lv_obj_set_style_text_color(s_menu_time, CLR_TEXT, 0);
    lv_obj_set_pos(s_menu_time, 12, 4);

    s_menu_loc = lv_label_create(card);
    lv_obj_set_style_text_color(s_menu_loc, CLR_ACCENT, 0);
    lv_obj_set_pos(s_menu_loc, 12, 22);

    char line[48];
    const sdmon_status_t *s = sd_monitor_get_status();
    snprintf(line, sizeof(line), "SD: %s   AUTO: %s", sd_state_text(s->state),
             s_auto ? "ON" : "OFF");
    lv_obj_t *st = lv_label_create(card);
    lv_label_set_text(st, line);
    lv_obj_set_style_text_color(st, state_color(s->state), 0);
    lv_obj_set_pos(st, 12, 40);

    // Initial fill
    refresh_menu_bottom();
}

// ---------------- SD screen ----------------
static void build_sd_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "SD CARD CHECK");
    make_back_btn(scr);

    s_sd_banner = lv_label_create(scr);
    lv_obj_set_style_text_font(s_sd_banner, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_sd_banner, CLR_WARN, 0);
    lv_obj_align(s_sd_banner, LV_ALIGN_TOP_MID, 0, 44);

    // Info card
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 300, 112);
    lv_obj_set_pos(card, 10, 66);
    lv_obj_set_style_bg_color(card, CLR_PANEL, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);

    s_sd_events = make_label(card, "", 12, 6, CLR_TEXT);
    s_sd_files  = make_label(card, "", 12, 26, CLR_TEXT);
    s_sd_cap    = make_label(card, "", 12, 46, CLR_TEXT);
    s_sd_sample = make_label(card, "", 12, 66, CLR_ACCENT);
    s_sd_bytes  = make_label(card, "", 12, 86, CLR_TEXT);
    s_sd_auto   = make_label(card, "", 160, 86, CLR_ACCENT);

    // Buttons
    lv_obj_t *scan = make_btn(scr, "SCAN", on_sd_scan, 0);
    lv_obj_set_size(scan, 140, 44);
    lv_obj_set_style_bg_color(scan, CLR_PRIMARY, 0);
    lv_obj_align(scan, LV_ALIGN_BOTTOM_LEFT, 10, -8);

    lv_obj_t *autob = make_btn(scr, s_auto ? "AUTO: ON" : "AUTO: OFF", on_sd_auto, 0);
    lv_obj_set_size(autob, 150, 44);
    lv_obj_set_style_bg_color(autob, s_auto ? CLR_ACCENT : CLR_PANEL, 0);
    lv_obj_set_style_bg_color(autob, CLR_PRIMARY_D, LV_STATE_PRESSED);
    lv_obj_align(autob, LV_ALIGN_BOTTOM_RIGHT, -10, -8);

    refresh_sd_labels();
}

const char *sd_state_text(sdmon_state_t st)
{
    switch (st) {
        case SDMON_READ_OK:    return "READ OK";
        case SDMON_EMPTY_OK:   return "ROOT READ OK";
        case SDMON_MOUNT_ERROR: return "NO CARD / ERR";
        case SDMON_READ_ERROR: return "READ ERROR";
        default:               return "NOT SCANNED";
    }
}

static lv_color_t state_color(sdmon_state_t st)
{
    switch (st) {
        case SDMON_READ_OK:
        case SDMON_EMPTY_OK:   return CLR_OK;
        case SDMON_MOUNT_ERROR:
        case SDMON_READ_ERROR: return CLR_ERR;
        default:               return CLR_WARN;
    }
}

static void refresh_sd_labels(void)
{
    if (s_screen != SCREEN_SD) return;
    const sdmon_status_t *s = sd_monitor_get_status();
    char line[48];

    if (s_scanning) {
        label_set_text_if_changed(s_sd_banner, "SCANNING...");
        lv_obj_set_style_text_color(s_sd_banner, CLR_WARN, 0);
    } else {
        const char *banner = sd_state_text(s->state);
        if ((s->state == SDMON_MOUNT_ERROR || s->state == SDMON_READ_ERROR) &&
            s->last_err == ESP_ERR_NO_MEM) {
            banner = "MEMORY BUSY - RETRY";
        } else if (s->state == SDMON_MOUNT_ERROR) {
            banner = "CHECK CARD / FAT32";
        }
        label_set_text_if_changed(s_sd_banner, banner);
        lv_obj_set_style_text_color(s_sd_banner, state_color(s->state), 0);
    }

    snprintf(line, sizeof(line), "READ EVENTS: %lu", (unsigned long)s->read_count);
    label_set_text_if_changed(s_sd_events, line);

    snprintf(line, sizeof(line), "FILES: %u", (unsigned)s->file_count);
    label_set_text_if_changed(s_sd_files, line);

    if (s->capacity_bytes) {
        unsigned long mb = (unsigned long)(s->capacity_bytes / (1024UL * 1024UL));
        if (mb >= 1024) {
            snprintf(line, sizeof(line), "CAPACITY: %lu.%02lu GB", mb / 1024, (mb % 1024) * 100 / 1024);
        } else {
            snprintf(line, sizeof(line), "CAPACITY: %lu MB", mb);
        }
    } else {
        snprintf(line, sizeof(line), "CAPACITY: --");
    }
    label_set_text_if_changed(s_sd_cap, line);

    if (s->sample_file[0]) {
        snprintf(line, sizeof(line), "SAMPLE: %.20s", s->sample_file);
    } else {
        snprintf(line, sizeof(line), "SAMPLE: --");
    }
    label_set_text_if_changed(s_sd_sample, line);

    if (s->last_err != ESP_OK && s->state != SDMON_IDLE) {
        snprintf(line, sizeof(line), "ERR: %s", esp_err_to_name(s->last_err));
    } else {
        snprintf(line, sizeof(line), "BYTES: %u", (unsigned)s->last_read_bytes);
    }
    label_set_text_if_changed(s_sd_bytes, line);

    if (s_auto) {
        int64_t remain = AUTO_PERIOD_US - (esp_timer_get_time() - s_last_scan_us);
        long secs = (long)((remain + 999999) / 1000000);
        if (secs < 0) secs = 0;
        if (secs > 3) secs = 3;
        snprintf(line, sizeof(line), "AUTO: ON (%lds)", secs);
    } else {
        snprintf(line, sizeof(line), "AUTO: OFF");
    }
    label_set_text_if_changed(s_sd_auto, line);
}

// ---------------- MENU bottom refresh ----------------
static void refresh_menu_bottom(void)
{
    if (s_screen != SCREEN_MENU) return;
    char line[64];
    char timebuf[32];

    if (net_time_str(timebuf, sizeof(timebuf))) {
        snprintf(line, sizeof(line), "%s", timebuf);
    } else {
        snprintf(line, sizeof(line), "TIME: syncing...");
    }
    label_set_text_if_changed(s_menu_time, line);

    const net_loc_t *loc = net_loc_get();
    net_loc_state_t ls = net_loc_get_state();
    if (loc && loc->ok) {
        snprintf(line, sizeof(line), "LOC: %.20s, %.20s", loc->city, loc->country);
        lv_obj_set_style_text_color(s_menu_loc, CLR_ACCENT, 0);
    } else if (ls == NET_LOC_QUERYING) {
        snprintf(line, sizeof(line), "LOC: locating...");
        lv_obj_set_style_text_color(s_menu_loc, CLR_TEXT_DIM, 0);
    } else if (ls == NET_LOC_FAILED) {
        // Fall back to showing the local IP address (from the router's DHCP).
        const char *ip = wifi_mgr_get_ip();
        if (ip && ip[0]) {
            snprintf(line, sizeof(line), "IP: %s", ip);
            lv_obj_set_style_text_color(s_menu_loc, CLR_TEXT, 0);
        } else {
            snprintf(line, sizeof(line), "LOC: unavailable");
            lv_obj_set_style_text_color(s_menu_loc, CLR_ERR, 0);
        }
    } else {
        snprintf(line, sizeof(line), "LOC: --");
        lv_obj_set_style_text_color(s_menu_loc, CLR_TEXT_DIM, 0);
    }
    label_set_text_if_changed(s_menu_loc, line);
}

// ---------------- SYSINFO screen ----------------
static void build_sysinfo_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "SYSTEM INFO");
    make_back_btn(scr);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 300, 140);
    lv_obj_set_pos(card, 10, 44);
    lv_obj_set_style_bg_color(card, CLR_PANEL, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);

    make_label(card, "CHIP: ESP32", 12, 6, CLR_TEXT);
    make_label(card, "FLASH: 4 MB", 12, 26, CLR_TEXT);
    make_label(card, "IDF: v5.5.4", 12, 46, CLR_TEXT);
    s_sys_uptime = make_label(card, "UPTIME: --", 12, 66, CLR_TEXT);
    s_sys_time   = make_label(card, "TIME: --", 12, 86, CLR_ACCENT);
    s_sys_wifi   = make_label(card, "WIFI: --", 12, 106, CLR_TEXT);
    s_sys_loc    = make_label(card, "LOC: --", 12, 126, CLR_TEXT);

    refresh_sysinfo_labels();
}

static void refresh_sysinfo_labels(void)
{
    if (s_screen != SCREEN_SYSINFO) return;
    char line[96];

    int64_t up_us = esp_timer_get_time();
    long up_s = (long)(up_us / 1000000);
    snprintf(line, sizeof(line), "UPTIME: %ldh %02ldm %02lds", up_s / 3600, (up_s % 3600) / 60, up_s % 60);
    label_set_text_if_changed(s_sys_uptime, line);

    char timebuf[32];
    if (net_time_str(timebuf, sizeof(timebuf))) {
        snprintf(line, sizeof(line), "TIME: %s", timebuf);
        label_set_text_if_changed(s_sys_time, line);
    } else {
        label_set_text_if_changed(s_sys_time, "TIME: syncing...");
    }

    wifi_state_t ws = wifi_mgr_get_state();
    if (ws == WIFI_STATE_CONNECTED) {
        snprintf(line, sizeof(line), "WIFI: %.20s [%s]", wifi_mgr_get_connected_ssid(), wifi_mgr_get_ip());
    } else if (ws == WIFI_STATE_CONNECTING) {
        snprintf(line, sizeof(line), "WIFI: connecting...");
    } else if (ws == WIFI_STATE_FAILED) {
        snprintf(line, sizeof(line), "WIFI: failed");
    } else {
        snprintf(line, sizeof(line), "WIFI: off");
    }
    label_set_text_if_changed(s_sys_wifi, line);

    const net_loc_t *loc = net_loc_get();
    if (loc && loc->ok) {
        snprintf(line, sizeof(line), "LOC: %.16s, %.16s (%s,%s)", loc->city, loc->country, loc->lat, loc->lon);
    } else {
        snprintf(line, sizeof(line), "LOC: querying...");
    }
    label_set_text_if_changed(s_sys_loc, line);
}

// ---------------- PC MONITOR screen (clock + host telemetry) ----------------
// Dashboard layout for 320x240:
//   y 9..31   title / accent line
//   y 36..74  big HH:MM:SS clock (montserrat 28)
//   y 80..98  date + weekday + UTC offset + time source
//   y 102..118  host link status
//   y 122..218 card with CPU / GPU / MEM load bars + temperatures
//   y 222..   hint line (tap to return)

// Any tap on empty space returns to the MENU (the screen doubles as the idle
// "clock" screen the device falls back to).
static void on_pcmon_tap(lv_event_t *e)
{
    (void)e;
    if (!click_ok()) return;
    lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_MENU);
}

static void fmt_tz_short(int offset_min, char *buf, size_t len)
{
    int a = offset_min < 0 ? -offset_min : offset_min;
    int h = a / 60, m = a % 60;
    if (m) snprintf(buf, len, "%s%d:%02d", offset_min < 0 ? "-" : "+", h, m);
    else   snprintf(buf, len, "%s%d", offset_min < 0 ? "-" : "+", h);
}

static lv_color_t load_color(int v)
{
    if (v < 0)  return CLR_PANEL_2;
    if (v >= 90) return CLR_ERR;
    if (v >= 70) return CLR_WARN;
    return CLR_OK;
}

static lv_obj_t *make_stat_bar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, 152, 14);
    lv_obj_set_style_bg_color(bar, CLR_PANEL_2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_KNOB);
    // Keep bars display-only so the tap-anywhere-to-return background catches
    // every touch.
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

// Big, high-contrast numeric value for one stat row (e.g. "42% 61°C").
static lv_obj_t *make_stat_value(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *val = lv_label_create(parent);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, CLR_TEXT, 0);
    lv_obj_set_size(val, 96, 20);
    lv_obj_set_pos(val, x, y);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    return val;
}

static void build_pcmon_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "PC MONITOR");

    // Tap-anywhere background (created first so every widget draws above it;
    // labels are not clickable, so taps on them fall through to here).
    lv_obj_t *bg = lv_obj_create(scr);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, CLR_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, on_pcmon_tap, LV_EVENT_CLICKED, NULL);

    s_pcmon_clock = lv_label_create(scr);
    lv_obj_set_style_text_font(s_pcmon_clock, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_pcmon_clock, CLR_TEXT, 0);
    lv_obj_set_size(s_pcmon_clock, 310, 38);
    lv_obj_set_pos(s_pcmon_clock, 5, 36);
    lv_obj_set_style_text_align(s_pcmon_clock, LV_TEXT_ALIGN_CENTER, 0);

    s_pcmon_date = lv_label_create(scr);
    lv_obj_set_style_text_color(s_pcmon_date, CLR_TEXT_DIM, 0);
    lv_obj_set_size(s_pcmon_date, 310, 18);
    lv_obj_set_pos(s_pcmon_date, 5, 80);
    lv_obj_set_style_text_align(s_pcmon_date, LV_TEXT_ALIGN_CENTER, 0);

    s_pcmon_link = lv_label_create(scr);
    lv_obj_set_style_text_color(s_pcmon_link, CLR_TEXT_DIM, 0);
    lv_obj_set_size(s_pcmon_link, 310, 18);
    lv_obj_set_pos(s_pcmon_link, 5, 102);
    lv_obj_set_style_text_align(s_pcmon_link, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 300, 96);
    lv_obj_set_pos(card, 10, 122);
    lv_obj_set_style_bg_color(card, CLR_PANEL, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    make_label(card, "CPU", 10, 4, CLR_TEXT_DIM);
    s_pcmon_cpu_bar = make_stat_bar(card, 46, 7);
    s_pcmon_cpu_val = make_stat_value(card, 200, 4);

    make_label(card, "GPU", 10, 34, CLR_TEXT_DIM);
    s_pcmon_gpu_bar = make_stat_bar(card, 46, 37);
    s_pcmon_gpu_val = make_stat_value(card, 200, 34);

    make_label(card, "MEM", 10, 64, CLR_TEXT_DIM);
    s_pcmon_mem_bar = make_stat_bar(card, 46, 67);
    s_pcmon_mem_val = make_stat_value(card, 200, 64);

    s_pcmon_mem_detail = lv_label_create(card);
    lv_obj_set_style_text_color(s_pcmon_mem_detail, CLR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_pcmon_mem_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(s_pcmon_mem_detail, 48, 80);
    lv_obj_set_size(s_pcmon_mem_detail, 240, 16);
    // Hide duplicate large MEMORY detail text. Keep the compact MEM percentage display.
    lv_obj_add_flag(s_pcmon_mem_detail, LV_OBJ_FLAG_HIDDEN);

    s_pcmon_status = lv_label_create(scr);
    lv_obj_set_style_text_color(s_pcmon_status, CLR_TEXT_DIM, 0);
    lv_obj_set_pos(s_pcmon_status, 5, 205);
    lv_obj_set_size(s_pcmon_status, 310, 16);
    lv_obj_set_style_text_align(s_pcmon_status, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *hint = lv_label_create(scr);
    // Keep to the glyphs Montserrat actually ships (0x20-0x7F + 0xB0/0x2022):
    // the U+00B7 middle dot is NOT in the font and would render as a box.
    lv_label_set_text(hint, "TAP TO RETURN  |  tools/pc_monitor_host.py");
    lv_obj_set_style_text_color(hint, CLR_TEXT_DIM, 0);
    lv_obj_set_size(hint, 310, 18);
    lv_obj_set_pos(hint, 5, 222);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    refresh_pcmon_screen();
}

static void refresh_one_stat(lv_obj_t *bar, lv_obj_t *val, int load, int temp)
{
    if (!bar || !val) return;
    lv_bar_set_value(bar, load >= 0 ? load : 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, load_color(load), LV_PART_INDICATOR);

    char line[32];
    if (load >= 0 && temp >= 0) {
        snprintf(line, sizeof(line), "%d%%  %d°C", load, temp);
    } else if (load >= 0) {
        snprintf(line, sizeof(line), "%d%%", load);
    } else if (temp >= 0) {
        snprintf(line, sizeof(line), "%d°C", temp);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    label_set_text_if_changed(val, line);
}

static void refresh_pcmon_screen(void)
{
    if (s_screen != SCREEN_PCMON) return;
    char buf[64];

    pc_monitor_status_t st;
    pc_monitor_get(&st);

    time_t now = 0;
    time(&now);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (net_time_synced() && tmv.tm_year >= 120) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        label_set_text_if_changed(s_pcmon_clock, buf);

        static const char *WD[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        char tzs[12];
        fmt_tz_short(net_tz_offset_minutes(), tzs, sizeof(tzs));
        const char *src = "?";
        net_time_src_t ts = net_time_source();
        if (ts == NET_TIME_SRC_USB) src = "USB";
        else if (ts == NET_TIME_SRC_WIFI) src = "WiFi";
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %s   UTC%s   via %s",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 WD[tmv.tm_wday], tzs, src);
        label_set_text_if_changed(s_pcmon_date, buf);
    } else {
        label_set_text_if_changed(s_pcmon_clock, "--:--:--");
        label_set_text_if_changed(s_pcmon_date,
                                  "NO TIME - connect USB or WiFi");
    }

    if (st.link) {
        snprintf(buf, sizeof(buf), "LINKED: %.20s  (%lus ago)",
                 st.host[0] ? st.host : "PC", (unsigned long)st.last_pkt_s);
        lv_obj_set_style_text_color(s_pcmon_link, CLR_ACCENT, 0);
    } else if (st.have_time || st.last_pkt_s > 0) {
        snprintf(buf, sizeof(buf), "HOST OFFLINE");
        lv_obj_set_style_text_color(s_pcmon_link, CLR_WARN, 0);
    } else {
        snprintf(buf, sizeof(buf), "WAITING FOR HOST...");
        lv_obj_set_style_text_color(s_pcmon_link, CLR_TEXT_DIM, 0);
    }
    label_set_text_if_changed(s_pcmon_link, buf);

    refresh_one_stat(s_pcmon_cpu_bar, s_pcmon_cpu_val, st.cpu_load, st.cpu_temp);
    refresh_one_stat(s_pcmon_gpu_bar, s_pcmon_gpu_val, st.gpu_load, st.gpu_temp);
    refresh_one_stat(s_pcmon_mem_bar, s_pcmon_mem_val, st.mem_load, -1);
    if (st.mem_used_mb >= 0 && st.mem_total_mb >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", st.mem_load);
        label_set_text_if_changed(s_pcmon_mem_val, buf);
        snprintf(buf, sizeof(buf), "MEMORY  %d MB / %d MB", st.mem_used_mb, st.mem_total_mb);
        label_set_text_if_changed(s_pcmon_mem_detail, buf);
    }

    if (s_pcmon_status) {
        label_set_text_if_changed(s_pcmon_status, buf);
    }
}

// ---------------- WIFI screen ----------------
// Two views: view 1 = AP list; view 2 = password entry + keyboard (after
// selecting an AP). The keyboard only appears in view 2 so it never covers
// the list or the CONNECT button.

static void build_wifi_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "WIFI");
    make_back_btn(scr);

    s_wifi_status = lv_label_create(scr);
    lv_obj_set_style_text_color(s_wifi_status, CLR_TEXT, 0);
    lv_obj_set_style_text_font(s_wifi_status, &font_cn16, 0);
    lv_obj_align(s_wifi_status, LV_ALIGN_TOP_LEFT, 10, 76);

    if (s_wifi_sel < 0) {
        // ---------- View 1: AP list ----------
        lv_obj_t *scan = make_btn(scr, "SCAN", on_wifi_scan, 0);
        lv_obj_set_size(scan, 80, 30);
        lv_obj_align(scan, LV_ALIGN_TOP_LEFT, 10, 40);

        // DISCONNECT button: only shown while connected.
        if (wifi_mgr_is_connected()) {
            lv_obj_t *disc = make_btn(scr, "DISCONNECT", on_wifi_disconnect, 0);
            lv_obj_set_size(disc, 110, 30);
            lv_obj_set_style_bg_color(disc, CLR_ERR, 0);
            lv_obj_set_style_bg_color(disc, CLR_PRIMARY_D, LV_STATE_PRESSED);
            lv_obj_align(disc, LV_ALIGN_TOP_RIGHT, -10, 40);
        }

        s_wifi_list = lv_list_create(scr);
        lv_obj_set_size(s_wifi_list, 300, 130);
        lv_obj_set_pos(s_wifi_list, 10, 92);
        lv_obj_set_style_bg_color(s_wifi_list, CLR_PANEL, 0);
        lv_obj_set_style_border_width(s_wifi_list, 0, 0);
        lv_obj_set_style_radius(s_wifi_list, 8, 0);
        lv_obj_set_style_text_color(s_wifi_list, CLR_TEXT, 0);
        s_wifi_ta = NULL;
        s_wifi_kb = NULL;
        refresh_wifi_labels();
    } else {
        // ---------- View 2: password + keyboard ----------
        // Status label goes up top; selected AP below it, then password box,
        // CONNECT, and the keyboard pinned to the bottom.
        lv_obj_align(s_wifi_status, LV_ALIGN_TOP_LEFT, 10, 44);

        wifi_ap_t ap;
        char line[96];
        if (wifi_mgr_get_ap(s_wifi_sel, &ap)) {
            snprintf(line, sizeof(line), "Selected: %.24s", ap.ssid);
        } else {
            snprintf(line, sizeof(line), "Selected AP");
        }
        lv_obj_t *sel = lv_label_create(scr);
        lv_label_set_text(sel, line);
        lv_obj_set_style_text_color(sel, CLR_ACCENT, 0);
        lv_obj_set_style_text_font(sel, &font_cn16, 0);
        lv_obj_align(sel, LV_ALIGN_TOP_LEFT, 10, 60);

        s_wifi_ta = lv_textarea_create(scr);
        lv_obj_set_size(s_wifi_ta, 300, 28);
        lv_obj_set_pos(s_wifi_ta, 10, 80);
        lv_textarea_set_placeholder_text(s_wifi_ta, "password (empty=open)");
        lv_textarea_set_password_mode(s_wifi_ta, true);
        lv_textarea_set_one_line(s_wifi_ta, true);
        lv_textarea_set_max_length(s_wifi_ta, WIFI_PASS_MAX);
        lv_obj_set_style_bg_color(s_wifi_ta, CLR_PANEL, 0);
        lv_obj_set_style_text_color(s_wifi_ta, CLR_TEXT, 0);
        lv_obj_set_style_border_color(s_wifi_ta, CLR_ACCENT, 0);
        lv_obj_set_style_radius(s_wifi_ta, 6, 0);

        lv_obj_t *conn = make_btn(scr, "CONNECT", on_wifi_connect, 0);
        lv_obj_set_size(conn, 300, 26);
        lv_obj_set_style_bg_color(conn, CLR_ACCENT, 0);
        lv_obj_set_style_bg_color(conn, CLR_PRIMARY_D, LV_STATE_PRESSED);
        lv_obj_align(conn, LV_ALIGN_TOP_LEFT, 10, 112);

        // Keyboard at the bottom with an explicit height so it is visible.
        s_wifi_kb = lv_keyboard_create(scr);
        lv_obj_set_size(s_wifi_kb, 320, 96);
        lv_obj_align(s_wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(s_wifi_kb, s_wifi_ta);

        s_wifi_list = NULL;
        refresh_wifi_labels();
    }
}

static void refresh_wifi_labels(void)
{
    if (s_screen != SCREEN_WIFI) return;
    char line[96];
    wifi_state_t ws = wifi_mgr_get_state();

    switch (ws) {
        case WIFI_STATE_CONNECTED:
            snprintf(line, sizeof(line), "Connected: %.20s  IP:%s", wifi_mgr_get_connected_ssid(), wifi_mgr_get_ip());
            lv_obj_set_style_text_color(s_wifi_status, CLR_OK, 0);
            break;
        case WIFI_STATE_CONNECTING:
            snprintf(line, sizeof(line), "Connecting...");
            lv_obj_set_style_text_color(s_wifi_status, CLR_WARN, 0);
            break;
        case WIFI_STATE_SCANNING:
            snprintf(line, sizeof(line), "Scanning...");
            lv_obj_set_style_text_color(s_wifi_status, CLR_WARN, 0);
            break;
        case WIFI_STATE_FAILED:
            snprintf(line, sizeof(line), "Connect failed");
            lv_obj_set_style_text_color(s_wifi_status, CLR_ERR, 0);
            break;
        default:
            snprintf(line, sizeof(line), "Not connected");
            lv_obj_set_style_text_color(s_wifi_status, CLR_TEXT_DIM, 0);
            break;
    }
    label_set_text_if_changed(s_wifi_status, line);

    // View 1: rebuild AP list when scan results are fresh, OR when the list
    // is empty but cached results exist (e.g. after a screen rebuild).
    bool fresh = wifi_mgr_poll();
    // NOTE: s_wifi_list may be NULL in view 2 — guard before dereferencing.
    bool cached = (s_wifi_list != NULL) && (lv_obj_get_child_cnt(s_wifi_list) == 0) && (wifi_mgr_get_ap_count() > 0);
    if (s_wifi_list && (fresh || cached)) {
        lv_obj_clean(s_wifi_list);
        int count = wifi_mgr_get_ap_count();
        for (int i = 0; i < count && i < 5; ++i) {
            wifi_ap_t ap;
            if (!wifi_mgr_get_ap(i, &ap)) break;
            lv_obj_t *b = lv_list_add_btn(s_wifi_list, NULL, ap.ssid);
            lv_obj_set_user_data(b, (void *)(intptr_t)i);
            lv_obj_set_style_bg_color(b, CLR_PANEL, 0);
            lv_obj_set_style_bg_color(b, CLR_PRIMARY_D, LV_STATE_PRESSED);
            lv_obj_set_style_text_color(b, CLR_TEXT, 0);
            lv_obj_add_event_cb(b, on_ap_click, LV_EVENT_CLICKED, NULL);
            // Use the CJK font for the label so Chinese SSIDs render.
            lv_obj_set_style_text_font(b, &font_cn16, 0);
            lv_obj_t *lbl = lv_obj_get_child(b, 0);
            if (lbl) lv_obj_set_style_text_font(lbl, &font_cn16, 0);
        }
        if (count == 0) {
            lv_obj_t *b = lv_list_add_btn(s_wifi_list, NULL, "(no AP found)");
            lv_obj_set_user_data(b, (void *)(intptr_t)-1);
            lv_obj_set_style_text_color(b, CLR_TEXT_DIM, 0);
            lv_obj_set_style_text_font(b, &font_cn16, 0);
            lv_obj_t *lbl = lv_obj_get_child(b, 0);
            if (lbl) lv_obj_set_style_text_font(lbl, &font_cn16, 0);
        }
    }
}

// ---------------- SUDOKU screen ----------------
#define SUDO_GRID_X   12
#define SUDO_GRID_Y   42
#define SUDO_CELL     18
#define SUDO_GRID     162   // 9 * 18

static void on_sudoku_cell(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    if (idx < 0 || idx >= 81) return;
    s_sudoku.sel_x = idx % 9;
    s_sudoku.sel_y = idx / 9;
    refresh_sudoku_board();
}

static void on_sudoku_digit(lv_event_t *e)
{
    if (!click_ok()) return;
    lv_obj_t *target = lv_event_get_target(e);
    int val = (int)(intptr_t)lv_obj_get_user_data(target);
    if (val < 1 || val > 9) return;
    bool conflict = sudoku_set(&s_sudoku, s_sudoku.sel_x, s_sudoku.sel_y, val);
    (void)conflict;
    refresh_sudoku_board();
}

static void on_sudoku_erase(lv_event_t *e)
{
    if (!click_ok()) return;
    sudoku_clear(&s_sudoku, s_sudoku.sel_x, s_sudoku.sel_y);
    refresh_sudoku_board();
}

static void on_sudoku_new(lv_event_t *e)
{
    if (!click_ok()) return;
    s_sudoku_puzzle = (s_sudoku_puzzle + 1) % SUDOKU_PUZZLES;
    sudoku_new(&s_sudoku, s_sudoku_puzzle);
    refresh_sudoku_board();
}

static void build_sudoku_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "SUDOKU");
    make_back_btn(scr);

    // Right column: digit keypad (1-9 vertical).
    const char *digits = "123456789";
    for (int i = 0; i < 9; ++i) {
        char buf[2] = {digits[i], '\0'};
        lv_obj_t *b = make_btn(scr, buf, on_sudoku_digit, digits[i] - '0');
        lv_obj_set_size(b, 56, 18);
        lv_obj_set_style_bg_color(b, CLR_PRIMARY, 0);
        lv_obj_set_style_bg_color(b, CLR_PRIMARY_D, LV_STATE_PRESSED);
        lv_obj_set_pos(b, 250, 42 + i * 19);
    }
    // DEL and NEW side by side below the keypad.
    lv_obj_t *er = make_btn(scr, "DEL", on_sudoku_erase, 0);
    lv_obj_set_size(er, 27, 20);
    lv_obj_set_style_bg_color(er, CLR_ERR, 0);
    lv_obj_set_style_bg_color(er, CLR_PRIMARY_D, LV_STATE_PRESSED);
    lv_obj_set_pos(er, 250, 42 + 9 * 19);

    lv_obj_t *newb = make_btn(scr, "NEW", on_sudoku_new, 0);
    lv_obj_set_size(newb, 27, 20);
    lv_obj_set_style_bg_color(newb, CLR_ACCENT, 0);
    lv_obj_set_style_bg_color(newb, CLR_PRIMARY_D, LV_STATE_PRESSED);
    lv_obj_set_pos(newb, 279, 42 + 9 * 19);

    // Status message line (below keypad).
    s_sudo_msg = lv_label_create(scr);
    lv_obj_set_style_text_color(s_sudo_msg, CLR_TEXT_DIM, 0);
    lv_obj_align(s_sudo_msg, LV_ALIGN_BOTTOM_LEFT, 12, -4);

    // Build the 9x9 board: thin separator lines + cell labels.
    // Background panel
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, SUDO_GRID + 2, SUDO_GRID + 2);
    lv_obj_set_pos(panel, SUDO_GRID_X - 1, SUDO_GRID_Y - 1);
    lv_obj_set_style_bg_color(panel, CLR_PANEL, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, CLR_TEXT_DIM, 0);
    lv_obj_set_style_radius(panel, 4, 0);

    // Cell objects (transparent bg, just to catch touches) + value labels.
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            int px = SUDO_GRID_X + x * SUDO_CELL;
            int py = SUDO_GRID_Y + y * SUDO_CELL;

            lv_obj_t *cell = lv_obj_create(scr);
            lv_obj_set_size(cell, SUDO_CELL, SUDO_CELL);
            lv_obj_set_pos(cell, px, py);
            lv_obj_set_style_bg_color(cell, CLR_BG, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_user_data(cell, (void *)(intptr_t)(y * 9 + x));
            lv_obj_add_event_cb(cell, on_sudoku_cell, LV_EVENT_CLICKED, NULL);
            s_sudo_cells[y][x] = cell;
        }
    }

    refresh_sudoku_board();
}

static void refresh_sudoku_board(void)
{
    if (s_screen != SCREEN_SUDOKU) return;

    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            lv_obj_t *cell = s_sudo_cells[y][x];
            if (!cell) continue;

            int px = SUDO_GRID_X + x * SUDO_CELL;
            int py = SUDO_GRID_Y + y * SUDO_CELL;

            // Selected highlight / clue background.
            if (x == s_sudoku.sel_x && y == s_sudoku.sel_y) {
                lv_obj_set_style_bg_color(cell, CLR_PRIMARY_D, 0);
            } else if (sudoku_is_given(&s_sudoku, x, y)) {
                lv_obj_set_style_bg_color(cell, CLR_PANEL, 0);
            } else {
                lv_obj_set_style_bg_color(cell, CLR_BG, 0);
            }

            char buf[2] = {0, 0};
            if (s_sudoku.grid[y][x] > 0) buf[0] = (char)('0' + s_sudoku.grid[y][x]);

            // Recreate or update the label inside the cell.
            lv_obj_t *lab = lv_obj_get_child(cell, 0);
            if (!lab) {
                lab = lv_label_create(cell);
                lv_obj_center(lab);
            }
            lv_label_set_text(lab, buf);
            if (sudoku_has_conflict(&s_sudoku, x, y)) {
                lv_obj_set_style_text_color(lab, CLR_ERR, 0);
            } else if (sudoku_is_given(&s_sudoku, x, y)) {
                lv_obj_set_style_text_color(lab, CLR_TEXT, 0);
            } else {
                lv_obj_set_style_text_color(lab, CLR_ACCENT, 0);
            }
            (void)px; (void)py;
        }
    }

    // Status message: win or hint.
    if (s_sudo_msg) {
        if (sudoku_is_complete(&s_sudoku)) {
            lv_label_set_text(s_sudo_msg, "Puzzle complete!  (NEW for another)");
            lv_obj_set_style_text_color(s_sudo_msg, CLR_OK, 0);
        } else {
            lv_label_set_text(s_sudo_msg, "Tap a cell, then a number 1-9");
            lv_obj_set_style_text_color(s_sudo_msg, CLR_TEXT_DIM, 0);
        }
    }
}

// ---------------- INVENTORY screen ----------------
// 3-column table (MODEL | QTY | PRODUCT NO) with filters, search, qty editing and
// stock history. Stock file is managed by the PC tool (INVENTORY.CSV).

#define INV_TBL_BG    lv_color_hex(0xF4F7FB)  // light table background
#define INV_TBL_BLACK lv_color_hex(0x11151C)  // black text on light bg

static bool inv_item_visible(const inv_item_t *it)
{
    if (!it) return false;
    if (s_inv_filter == 1 && it->qty <= 0) return false;
    if (s_inv_filter == 2 && it->qty > 0) return false;
    if (s_inv_query[0] &&
        strcasestr(it->model, s_inv_query) == NULL &&
        strcasestr(it->product_no, s_inv_query) == NULL) {
        return false;
    }
    return true;
}

static void inv_update_filter_buttons(void)
{
    for (int i = 0; i < 4; ++i) {
        if (!s_inv_filter_btn[i]) continue;
        lv_obj_set_style_bg_color(s_inv_filter_btn[i],
                                  i == s_inv_filter ? CLR_ACCENT : CLR_PANEL, 0);
    }
}

static void on_inv_filter(lv_event_t *e)
{
    if (!click_ok()) return;
    lv_obj_t *target = lv_event_get_target(e);
    int mode = (int)(intptr_t)lv_obj_get_user_data(target);
    if (mode < 0 || mode > 3) return;

    s_inv_filter = mode;
    s_inv_query[0] = '\0';
    s_inv_edit_idx = -1;
    inv_update_filter_buttons();

    if (s_inv_list) lv_obj_scroll_to_y(s_inv_list, 0, LV_ANIM_OFF);
    refresh_inventory_list();
}

// Char-select search panel (single lv_btnmatrix — memory-friendly, stable).
static lv_obj_t *s_inv_q_lbl;   // shows the current query inside the panel

// Keypad layout: letters / digits / actions. "" ends a row, "\n" is a row break.
static const char *inv_kb_map[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "\n",
    "J", "K", "L", "M", "N", "O", "P", "Q", "R", "\n",
    "S", "T", "U", "V", "W", "X", "Y", "Z", LV_SYMBOL_BACKSPACE, "\n",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "\n",
    "-", ".", "_", "/", " ", LV_SYMBOL_OK, LV_SYMBOL_CLOSE, ""
};

// Which keys get wider buttons (index into the flat map array above).
#define KB_WIDE_BS 27   // backspace
#define KB_WIDE_OK 40   // OK (LV_SYMBOL_OK)
#define KB_WIDE_CL 41   // close (LV_SYMBOL_CLOSE)

static void inv_close_search(bool apply)
{
    if (!apply) s_inv_query[0] = '\0';

    // Defer deletion because this helper is called from a child button-matrix
    // event. This avoids deleting the object whose callback is still running.
    if (s_inv_edit_overlay) {
        lv_obj_del_async(s_inv_edit_overlay);
        s_inv_edit_overlay = NULL;
        s_inv_q_lbl = NULL;
    }

    s_inv_edit_idx = -1;
    if (s_inv_list) lv_obj_scroll_to_y(s_inv_list, 0, LV_ANIM_OFF);
    refresh_inventory_list();
}

static void on_inv_kb(lv_event_t *e)
{
    if (!click_ok()) return;
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char *txt = lv_btnmatrix_get_btn_text(kb, id);
    if (!txt) return;

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        size_t len = strlen(s_inv_query);
        if (len > 0) s_inv_query[len - 1] = '\0';
        if (s_inv_q_lbl) label_set_text_if_changed(s_inv_q_lbl, s_inv_query);
    } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        inv_close_search(true);
    } else if (strcmp(txt, LV_SYMBOL_CLOSE) == 0) {
        inv_close_search(false);
    } else if (txt[0] && txt[1] == '\0') {
        size_t len = strlen(s_inv_query);
        if (len < sizeof(s_inv_query) - 1) {
            s_inv_query[len] = txt[0];
            s_inv_query[len + 1] = '\0';
        }
        if (s_inv_q_lbl) label_set_text_if_changed(s_inv_q_lbl, s_inv_query);
    }
}

static void on_inv_search(lv_event_t *e)
{
    if (!click_ok()) return;
    ESP_LOGI(TAG, "inv: opening search panel, query='%s'", s_inv_query);
    s_inv_edit_idx = -1;

    lv_obj_t *scr = lv_scr_act();
    s_inv_edit_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_inv_edit_overlay, 320, 240);
    lv_obj_set_pos(s_inv_edit_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_inv_edit_overlay, lv_color_hex(0x10141C), 0);
    lv_obj_set_style_border_width(s_inv_edit_overlay, 0, 0);
    lv_obj_set_style_radius(s_inv_edit_overlay, 0, 0);
    lv_obj_clear_flag(s_inv_edit_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(s_inv_edit_overlay);
    lv_label_set_text(lbl, "Filter MODEL / PRODUCT NO");
    lv_obj_set_style_text_color(lbl, CLR_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 6);

    s_inv_q_lbl = lv_label_create(s_inv_edit_overlay);
    lv_label_set_text(s_inv_q_lbl, s_inv_query);
    lv_obj_set_style_text_color(s_inv_q_lbl, CLR_ACCENT, 0);
    lv_obj_set_style_bg_color(s_inv_q_lbl, CLR_PANEL, 0);
    lv_obj_set_style_border_width(s_inv_q_lbl, 0, 0);
    lv_obj_set_style_radius(s_inv_q_lbl, 4, 0);
    lv_obj_set_pos(s_inv_q_lbl, 10, 26);
    lv_obj_set_size(s_inv_q_lbl, 300, 22);

    lv_obj_t *kb = lv_btnmatrix_create(s_inv_edit_overlay);
    lv_btnmatrix_set_map(kb, inv_kb_map);
    lv_obj_set_size(kb, 300, 180);
    lv_obj_set_pos(kb, 10, 54);
    lv_obj_set_style_bg_color(kb, CLR_PANEL, 0);
    lv_obj_set_style_text_color(kb, CLR_TEXT, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_radius(kb, 8, 0);
    lv_obj_set_style_pad_row(kb, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb, CLR_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, on_inv_kb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_btnmatrix_set_btn_width(kb, KB_WIDE_BS, 2);
    lv_btnmatrix_set_btn_width(kb, KB_WIDE_OK, 2);
    lv_btnmatrix_set_btn_width(kb, KB_WIDE_CL, 2);
    lv_btnmatrix_set_btn_ctrl(kb, KB_WIDE_OK, LV_BTNMATRIX_CTRL_POPOVER);
    lv_btnmatrix_set_btn_ctrl(kb, KB_WIDE_CL, LV_BTNMATRIX_CTRL_POPOVER);
}

static void on_inv_qty_minus(lv_event_t *e)
{
    if (!click_ok()) return;
    if (s_inv_edit_idx < 0 || s_inv_edit_idx >= s_inv.count) return;
    if (s_inv.items[s_inv_edit_idx].qty > 0) {
        s_inv.items[s_inv_edit_idx].qty -= 1;
        inv_save(&s_inv);
        char stamp[32];
        net_time_str(stamp, sizeof(stamp));
        inv_hist_append(stamp, "OUT",
                        s_inv.items[s_inv_edit_idx].model,
                        s_inv.items[s_inv_edit_idx].product_no, 1);
        ESP_LOGI(TAG, "inv: qty-- idx=%d now=%ld", s_inv_edit_idx,
                 (long)s_inv.items[s_inv_edit_idx].qty);
    }
    refresh_inventory_list();
}

static void on_inv_qty_plus(lv_event_t *e)
{
    if (!click_ok()) return;
    if (s_inv_edit_idx < 0 || s_inv_edit_idx >= s_inv.count) return;
    s_inv.items[s_inv_edit_idx].qty += 1;
    inv_save(&s_inv);
    char stamp[32];
    net_time_str(stamp, sizeof(stamp));
    inv_hist_append(stamp, "IN",
                    s_inv.items[s_inv_edit_idx].model,
                    s_inv.items[s_inv_edit_idx].product_no, 1);
    ESP_LOGI(TAG, "inv: qty++ idx=%d now=%ld", s_inv_edit_idx,
             (long)s_inv.items[s_inv_edit_idx].qty);
    refresh_inventory_list();
}

static void on_inv_row_click(lv_event_t *e)
{
    if (!click_ok()) return;
    lv_obj_t *target = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    if (idx < 0 || idx >= s_inv.count) return;
    s_inv_edit_idx = idx;
    ESP_LOGI(TAG, "inv: selected idx=%d for qty edit", s_inv_edit_idx);
    refresh_inventory_window(true);
}

// Clicking anywhere outside the list clears the row selection (hides -/+ buttons).
static void on_inv_bg_click(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (s_inv_edit_idx < 0) return;

    lv_obj_t *p = target;
    bool in_list = false;
    while (p) {
        if (p == s_inv_list) { in_list = true; break; }
        p = lv_obj_get_parent(p);
    }
    if (!in_list) {
        s_inv_edit_idx = -1;
        refresh_inventory_window(true);
    }
}

static void on_inv_del(lv_event_t *e)
{
    if (!click_ok()) return;
    if (s_inv_edit_idx < 0 || s_inv_edit_idx >= s_inv.count) return;
    inv_item_t *it = &s_inv.items[s_inv_edit_idx];
    char stamp[32];
    net_time_str(stamp, sizeof(stamp));
    bool hist_ok = inv_hist_append(stamp, "DEL", it->model, it->product_no, it->qty);
    ESP_LOGI(TAG, "inv: DEL idx=%d model=%s product=%s qty=%ld hist=%d",
             s_inv_edit_idx, it->model, it->product_no, (long)it->qty, (int)hist_ok);

    for (int i = s_inv_edit_idx; i + 1 < s_inv.count; ++i) {
        s_inv.items[i] = s_inv.items[i + 1];
    }
    s_inv.count--;
    inv_save(&s_inv);
    s_inv_edit_idx = -1;
    refresh_inventory_list();
}

static void on_inv_scroll(lv_event_t *e)
{
    (void)e;
    refresh_inventory_window(false);
}

static void inv_create_row_pool(void)
{
    // A 1x1 object placed at the logical end gives LVGL the full scroll range
    // without allocating hundreds of row widgets.
    s_inv_extent = lv_obj_create(s_inv_list);
    lv_obj_set_size(s_inv_extent, 1, 1);
    lv_obj_set_pos(s_inv_extent, 291, 0);
    lv_obj_set_style_bg_opa(s_inv_extent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_inv_extent, 0, 0);
    lv_obj_set_style_pad_all(s_inv_extent, 0, 0);
    lv_obj_clear_flag(s_inv_extent, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int slot = 0; slot < INV_ROW_POOL; ++slot) {
        inv_row_ui_t *r = &s_inv_rows[slot];
        memset(r, 0, sizeof(*r));

        r->row = lv_obj_create(s_inv_list);
        lv_obj_set_size(r->row, 292, 20);
        lv_obj_set_style_radius(r->row, 4, 0);
        lv_obj_set_style_border_width(r->row, 0, 0);
        lv_obj_set_style_pad_all(r->row, 0, 0);
        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(r->row, on_inv_row_click, LV_EVENT_CLICKED, NULL);

        r->model = lv_label_create(r->row);
        lv_label_set_long_mode(r->model, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(r->model, INV_TBL_BLACK, 0);
        lv_obj_set_style_text_align(r->model, LV_TEXT_ALIGN_LEFT, 0);

        r->qty = lv_label_create(r->row);
        lv_obj_set_style_text_color(r->qty, INV_TBL_BLACK, 0);
        lv_obj_set_style_text_align(r->qty, LV_TEXT_ALIGN_CENTER, 0);

        r->product_no = lv_label_create(r->row);
        lv_label_set_long_mode(r->product_no, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(r->product_no, INV_TBL_BLACK, 0);
        lv_obj_set_style_text_align(r->product_no, LV_TEXT_ALIGN_LEFT, 0);

        r->minus = make_btn(r->row, "-", on_inv_qty_minus, 0);
        lv_obj_set_size(r->minus, 22, 18);
        lv_obj_set_pos(r->minus, 222, 1);
        lv_obj_set_style_bg_color(r->minus, CLR_ERR, 0);

        r->plus = make_btn(r->row, "+", on_inv_qty_plus, 0);
        lv_obj_set_size(r->plus, 22, 18);
        lv_obj_set_pos(r->plus, 246, 1);
        lv_obj_set_style_bg_color(r->plus, CLR_OK, 0);

        r->del = make_btn(r->row, LV_SYMBOL_TRASH, on_inv_del, 0);
        lv_obj_set_size(r->del, 22, 18);
        lv_obj_set_pos(r->del, 270, 1);
        lv_obj_set_style_bg_color(r->del, lv_color_hex(0x8A2BE2), 0);

        lv_obj_add_flag(r->minus, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(r->plus, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(r->del, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_inventory_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "INVENTORY");
    make_back_btn(scr);

    const char *labels[4] = { "ALL", "IN", "OUT", "HIST" };
    for (int i = 0; i < 4; ++i) {
        s_inv_filter_btn[i] = make_btn(scr, labels[i], on_inv_filter, i);
        lv_obj_set_size(s_inv_filter_btn[i], 46, 22);
        lv_obj_set_pos(s_inv_filter_btn[i], 10 + i * 50, 40);
        lv_obj_set_style_bg_color(s_inv_filter_btn[i],
                                  i == s_inv_filter ? CLR_ACCENT : CLR_PANEL, 0);
        lv_obj_set_style_bg_color(s_inv_filter_btn[i], CLR_PRIMARY_D, LV_STATE_PRESSED);
    }

    s_inv_stats = lv_label_create(scr);
    lv_obj_set_style_text_color(s_inv_stats, CLR_TEXT, 0);
    lv_obj_set_pos(s_inv_stats, 10, 66);
    lv_obj_set_size(s_inv_stats, 300, 16);
    lv_label_set_long_mode(s_inv_stats, LV_LABEL_LONG_DOT);

    lv_obj_t *h1 = make_btn(scr, "MODEL", on_inv_search, 0);
    lv_obj_set_size(h1, 70, 18);
    lv_obj_set_pos(h1, 10, 82);
    lv_obj_set_style_bg_color(h1, CLR_PANEL, 0);
    lv_obj_set_style_text_color(h1, CLR_ACCENT, 0);
    lv_obj_t *h2 = lv_label_create(scr);
    lv_label_set_text(h2, "QTY");
    lv_obj_set_style_text_color(h2, CLR_ACCENT, 0);
    lv_obj_set_pos(h2, 172, 85);
    lv_obj_t *h3 = make_btn(scr, "PART#", on_inv_search, 0);
    lv_obj_set_size(h3, 70, 18);
    lv_obj_set_pos(h3, 212, 82);
    lv_obj_set_style_bg_color(h3, CLR_PANEL, 0);
    lv_obj_set_style_text_color(h3, CLR_ACCENT, 0);

    s_inv_list = lv_obj_create(scr);
    lv_obj_set_size(s_inv_list, 300, 132);
    lv_obj_set_pos(s_inv_list, 10, 104);
    lv_obj_set_style_bg_color(s_inv_list, INV_TBL_BG, 0);
    lv_obj_set_style_border_width(s_inv_list, 0, 0);
    lv_obj_set_style_radius(s_inv_list, 8, 0);
    lv_obj_set_style_pad_all(s_inv_list, 4, 0);
    lv_obj_set_scroll_dir(s_inv_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_inv_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_text_color(s_inv_list, INV_TBL_BLACK, 0);
    lv_obj_add_event_cb(s_inv_list, on_inv_scroll, LV_EVENT_SCROLL, NULL);

    s_inv_first_row = -1;
    s_inv_view_count = 0;
    s_inv_hist_count = 0;
    inv_create_row_pool();

    lv_obj_add_event_cb(scr, on_inv_bg_click, LV_EVENT_CLICKED, NULL);
    refresh_inventory_list();
}

static void refresh_inventory_window(bool force)
{
    if (s_screen != SCREEN_INVENTORY || !s_inv_list) return;

    int logical_count = (s_inv_filter == 3) ? s_inv_hist_count : s_inv_view_count;
    int sy = (int)lv_obj_get_scroll_y(s_inv_list);
    if (sy < 0) sy = 0;

    int first = sy / INV_ROW_H;
    if (first > 0) first--; // keep one partially-visible row above the viewport
    int max_first = logical_count > INV_ROW_POOL ? logical_count - INV_ROW_POOL : 0;
    if (first > max_first) first = max_first;
    if (first < 0) first = 0;

    if (!force && first == s_inv_first_row) return;
    s_inv_first_row = first;

    for (int slot = 0; slot < INV_ROW_POOL; ++slot) {
        inv_row_ui_t *r = &s_inv_rows[slot];
        int view_pos = first + slot;

        if (!r->row || view_pos >= logical_count) {
            if (r->row) lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(r->row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(r->row, 0, 4 + view_pos * INV_ROW_H);

        if (s_inv_filter == 3) {
            int hidx = s_inv_hist_count - 1 - view_pos; // newest first
            if (hidx < 0 || hidx >= s_inv_hist_count) {
                lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            const inv_hist_t *h = &s_inv_hist_cache[hidx];
            const char *act = h->action;
            bool is_in  = (act[0] == 'I');
            bool is_del = (act[0] == 'D');
            /*
             * History line worst case is over 96 bytes:
             * action + timestamp + 63-byte model + quantity.
             * ESP-IDF enables -Werror=format-truncation, so keep enough room
             * for the full formatted text instead of relying on truncation.
             */
            char line[160];

            if (is_del) {
                snprintf(line, sizeof(line), "%s %s %s x%ld", LV_SYMBOL_TRASH,
                         h->stamp, h->model[0] ? h->model : h->product_no, (long)h->qty);
            } else {
                snprintf(line, sizeof(line), "%s %s %s x%ld", act,
                         h->stamp, h->model[0] ? h->model : h->product_no, (long)h->qty);
            }

            label_set_text_if_changed(r->model, line);
            lv_obj_set_pos(r->model, 6, 2);
            lv_obj_set_size(r->model, 278, 16);
            lv_obj_add_flag(r->qty, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->product_no, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->minus, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->plus, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->del, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(r->row, (void *)(intptr_t)-1);
            lv_obj_set_style_bg_color(r->row,
                is_in  ? lv_color_hex(0xE8F8EE) :
                is_del ? lv_color_hex(0xFFC9C9) : lv_color_hex(0xFFE9E9), 0);
            continue;
        }

        int idx = s_inv_view_indices[view_pos];
        if (idx < 0 || idx >= s_inv.count) {
            lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const inv_item_t *it = &s_inv.items[idx];
        bool selected = (idx == s_inv_edit_idx);

        lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r->row, (void *)(intptr_t)idx);
        lv_obj_clear_flag(r->qty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(r->product_no, LV_OBJ_FLAG_HIDDEN);

        int c1w = selected ? 100 : 168;
        int c2x = selected ? 110 : 178;
        int c2w = selected ? 38 : 42;
        int c3x = selected ? 150 : 224;
        int c3w = selected ? 68 : 62;

        label_set_text_if_changed(r->model, it->model[0] ? it->model : it->product_no);
        lv_obj_set_pos(r->model, 6, 3);
        lv_obj_set_size(r->model, c1w, 16);

        char qbuf[16];
        snprintf(qbuf, sizeof(qbuf), "x%ld", (long)it->qty);
        label_set_text_if_changed(r->qty, qbuf);
        lv_obj_set_pos(r->qty, c2x, 3);
        lv_obj_set_size(r->qty, c2w, 16);

        label_set_text_if_changed(r->product_no, it->product_no);
        lv_obj_set_pos(r->product_no, c3x, 3);
        lv_obj_set_size(r->product_no, c3w, 16);

        if (selected) {
            lv_obj_set_style_bg_color(r->row, lv_color_hex(0xBFE3FF), 0);
            lv_obj_clear_flag(r->minus, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(r->plus, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(r->del, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_style_bg_color(r->row,
                                      it->qty > 0 ? lv_color_hex(0xFFFFFF)
                                                  : lv_color_hex(0xFFE9E9), 0);
            lv_obj_add_flag(r->minus, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->plus, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->del, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_inventory_list(void)
{
    if (s_screen != SCREEN_INVENTORY || !s_inv_list) return;

    int total = 0, in_stock = 0, out_stock = 0;
    long total_qty = 0;
    for (int i = 0; i < s_inv.count; ++i) {
        const inv_item_t *it = &s_inv.items[i];
        total++;
        total_qty += it->qty;
        if (it->qty > 0) in_stock++; else out_stock++;
    }

    s_inv_view_count = 0;
    s_inv_hist_count = 0;

    if (s_inv_filter == 3) {
        s_inv_hist_count = inv_hist_load(s_inv_hist_cache, INV_HIST_MAX);
        char line[64];
        snprintf(line, sizeof(line), "History: %d entries", s_inv_hist_count);
        label_set_text_if_changed(s_inv_stats, line);
    } else {
        for (int i = 0; i < s_inv.count && s_inv_view_count < INV_MAX_ITEMS; ++i) {
            if (inv_item_visible(&s_inv.items[i])) {
                s_inv_view_indices[s_inv_view_count++] = i;
            }
        }

        char line[96];
        if (s_inv_view_count != total) {
            snprintf(line, sizeof(line), "%d/%d itm | %d ok | %d out | %ld pcs",
                     s_inv_view_count, total, in_stock, out_stock, total_qty);
        } else {
            snprintf(line, sizeof(line), "%d itm | %d ok | %d out | %ld pcs",
                     total, in_stock, out_stock, total_qty);
        }
        label_set_text_if_changed(s_inv_stats, line);
    }

    int logical_count = (s_inv_filter == 3) ? s_inv_hist_count : s_inv_view_count;
    if (s_inv_extent) {
        int y = logical_count > 0 ? 4 + logical_count * INV_ROW_H : 4;
        lv_obj_set_pos(s_inv_extent, 291, y);
    }

    // Re-apply the current scroll value so LVGL clamps it if filtering or
    // deletion shortened the logical list.
    lv_coord_t sy = lv_obj_get_scroll_y(s_inv_list);
    if (sy < 0) sy = 0;
    lv_obj_scroll_to_y(s_inv_list, sy, LV_ANIM_OFF);

    s_inv_first_row = -1;
    refresh_inventory_window(true);
}


// ---------------- UTF-8 display helpers ----------------
static uint32_t ui_utf8_next(const char **pp)
{
    const unsigned char *p = (const unsigned char *)(pp && *pp ? *pp : "");
    if (!*p) return 0;
    uint32_t cp = '?'; size_t n = 1;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F); n = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2] &&
               (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F); n = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3] &&
               (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
             ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F); n = 4;
    }
    *pp += n;
    return cp;
}

static void ui_put_ascii(char *out, size_t out_len, size_t *used, const char *text)
{
    if (!out || !used || !text) return;
    while (*text && *used + 1 < out_len) out[(*used)++] = *text++;
    out[*used] = '\0';
}

static void ui_put_cp(char *out, size_t out_len, size_t *used, uint32_t cp)
{
    unsigned char b[4]; size_t n = 0;
    if (cp < 0x80) { b[0] = (unsigned char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3F); n = 2; }
    else if (cp < 0x10000) { b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F); b[2] = 0x80 | (cp & 0x3F); n = 3; }
    else { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F); b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); n = 4; }
    if (*used + n >= out_len) return;
    memcpy(out + *used, b, n); *used += n; out[*used] = '\0';
}

// Montserrat doesn't contain IPA. Convert common IPA symbols to a compact,
// readable ASCII notation instead of rendering missing-glyph boxes/garbage.
static void format_phonetic_display(const char *src, char *out, size_t out_len)
{
    if (!out || !out_len) {
        return;
    }
    out[0] = '\0';
    const char *p = src ? src : ""; size_t used = 0;
    while (*p && used + 1 < out_len) {
        uint32_t cp = ui_utf8_next(&p); const char *rep = NULL;
        switch (cp) {
            case 0x0259: rep = "@"; break;   // ə
            case 0x026A: rep = "I"; break;   // ɪ
            case 0x028A: rep = "U"; break;   // ʊ
            case 0x028C: rep = "^"; break;   // ʌ
            case 0x0251: rep = "a"; break;   // ɑ
            case 0x0254: rep = "O"; break;   // ɔ
            case 0x025C: rep = "3"; break;   // ɜ
            case 0x025D: rep = "3r"; break;  // ɝ
            case 0x025A: rep = "@r"; break;  // ɚ
            case 0x0250: rep = "a"; break;   // ɐ
            case 0x0258: rep = "@"; break;   // ɘ
            case 0x025E: rep = "O"; break;   // ɞ
            case 0x00E6: rep = "ae"; break;  // æ
            case 0x0283: rep = "sh"; break;  // ʃ
            case 0x0292: rep = "zh"; break;  // ʒ
            case 0x03B8: rep = "th"; break;  // θ
            case 0x00F0: rep = "dh"; break;  // ð
            case 0x014B: rep = "ng"; break;  // ŋ
            case 0x0279: rep = "r"; break;   // ɹ
            case 0x027E: rep = "r"; break;   // ɾ
            case 0x028B: rep = "v"; break;   // ʋ
            case 0x025B: rep = "e"; break;   // ɛ
            case 0x0268: rep = "i"; break;   // ɨ
            case 0x0289: rep = "u"; break;   // ʉ
            case 0x026F: rep = "u"; break;   // ɯ
            case 0x0264: rep = "o"; break;   // ɤ
            case 0x0275: rep = "o"; break;   // ɵ
            case 0x0252: rep = "o"; break;   // ɒ
            case 0x0261: rep = "g"; break;   // ɡ
            case 0x02C8: rep = "'"; break;   // primary stress
            case 0x02CC: rep = ","; break;   // secondary stress
            case 0x02D0: rep = ":"; break;   // length
            case 0x02B0: rep = "h"; break;   // aspiration ʰ
            case 0x0303: rep = "~"; break;   // nasalization combining tilde
            case 0x0329: rep = ""; break;    // syllabic combining mark
            default: break;
        }
        if (rep) ui_put_ascii(out, out_len, &used, rep);
        else if (cp >= 0x20 && cp < 0x7F) { char a[2] = {(char)cp, 0}; ui_put_ascii(out, out_len, &used, a); }
        else ui_put_ascii(out, out_len, &used, "?");
    }
}

static void format_cn_display(const char *src, char *out, size_t out_len)
{
    if (!out || !out_len) {
        return;
    }
    out[0] = '\0';
    const char *p = src ? src : ""; size_t used = 0;
    while (*p && used + 1 < out_len) {
        uint32_t cp = ui_utf8_next(&p);
        if (cp < 0x20) continue;
        if (cp < 0x80) { char a[2] = {(char)cp, 0}; ui_put_ascii(out, out_len, &used, a); continue; }
        lv_font_glyph_dsc_t gd;
        if (lv_font_get_glyph_dsc(&font_cn16, &gd, cp, 0)) ui_put_cp(out, out_len, &used, cp);
        else ui_put_ascii(out, out_len, &used, "?");
    }
}

// ---------------- VOCAB LAB ----------------
// A compact, touch-first vocabulary workflow inspired by modern spaced-
// repetition apps. The implementation is intentionally offline-first: books and
// progress live on SD, while only optional wordbook downloads need Wi-Fi.

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CLR_PANEL, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x263241), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void set_btn_text(lv_obj_t *btn, const char *text)
{
    if (!btn) return;
    lv_obj_t *lab = lv_obj_get_child(btn, 0);
    if (lab) lv_label_set_text(lab, text ? text : "");
}

static void on_vocab_home_btn(lv_event_t *e)
{
    if (!click_ok()) return;
    int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    s_vocab_home_notice[0] = '\0';
    if (id == 0 || id == 1) {
        if (!vocab_has_active_book()) {
            snprintf(s_vocab_home_notice, sizeof(s_vocab_home_notice), "Select a wordbook first");
            refresh_vocab_dashboard();
            return;
        }
        vocab_mode_t mode = id == 0 ? VOCAB_MODE_LEARN : VOCAB_MODE_DICTATION;
        esp_err_t err = vocab_open_book_async(vocab_active_book(), mode);
        if (err != ESP_OK) snprintf(s_vocab_home_notice, sizeof(s_vocab_home_notice), "Wordbook worker is busy");
        refresh_vocab_dashboard();
        return;
    }
    switch (id) {
        case 2: lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_VOCAB_BOOKS); break;
        case 3: lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_VOCAB_PLAN); break;
        case 4: lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_SD); break;
        default: break;
    }
}

static void refresh_vocab_dashboard(void)
{
    if (s_screen != SCREEN_VOCAB || !s_vocab_status) return;
    const sdmon_status_t *sd = sd_monitor_get_status();
    if (!sd || !sd->mounted) {
        label_set_text_if_changed(s_vocab_status, "SD card is not mounted. Run SD CHECK first.");
        lv_obj_set_style_text_color(s_vocab_status, CLR_WARN, 0); return;
    }
    if (s_vocab_home_notice[0]) {
        label_set_text_if_changed(s_vocab_status, s_vocab_home_notice);
        lv_obj_set_style_text_color(s_vocab_status, CLR_WARN, 0); return;
    }
    const vocab_open_status_t *op = vocab_open_status();
    if (op && op->state >= VOCAB_OPEN_PREPARING && op->state <= VOCAB_OPEN_PLANNING) {
        char b[150];
        if (op->state == VOCAB_OPEN_IMPORTING && op->bytes_total) {
            unsigned pct = (unsigned)((op->bytes_done * 100ULL) / op->bytes_total); if (pct > 100) pct = 100;
            snprintf(b, sizeof(b), "PREPARING %u%% | %lu words\n%s", pct, (unsigned long)op->words_done, op->message);
        } else snprintf(b, sizeof(b), "PREPARING WORD SESSION\n%s", op->message);
        label_set_text_if_changed(s_vocab_status, b); lv_obj_set_style_text_color(s_vocab_status, CLR_PRIMARY, 0); return;
    }
    if (!vocab_has_active_book()) {
        label_set_text_if_changed(s_vocab_status, "No active wordbook. Open BOOKS to choose one.");
        lv_obj_set_style_text_color(s_vocab_status, CLR_TEXT_DIM, 0); return;
    }
    vocab_stats_t st; vocab_get_stats(&st);
    const char *date_tag = net_time_synced() ? "DATE OK" : (vocab_daily_date_known() ? "DATE CACHED" : "TIME NEEDED");
    char buf[180];
    snprintf(buf, sizeof(buf), "%s | %s\n%lu learned | %lu due | %u / %u today",
             vocab_active_book(), date_tag, (unsigned long)st.learned_words, (unsigned long)st.due_words,
             (unsigned)st.today_done, (unsigned)st.daily_target);
    label_set_text_if_changed(s_vocab_status, buf);
    lv_obj_set_style_text_color(s_vocab_status, net_time_synced() ? CLR_TEXT : CLR_WARN, 0);
}

static void build_vocab_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);
    make_title(scr, "VOCAB LAB");
    make_back_btn(scr);

    lv_obj_t *hero = make_card(scr, 10, 43, 300, 74);
    lv_obj_t *kicker = make_label(hero, "OFFLINE WORD TRAINER", 12, 8, CLR_PRIMARY);
    lv_obj_set_style_text_font(kicker, &lv_font_montserrat_14, 0);
    s_vocab_status = lv_label_create(hero);
    lv_obj_set_pos(s_vocab_status, 12, 29);
    lv_obj_set_size(s_vocab_status, 276, 42);
    lv_label_set_long_mode(s_vocab_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_vocab_status, CLR_TEXT, 0);

    lv_obj_t *learn = make_btn(scr, "LEARN", on_vocab_home_btn, 0);
    lv_obj_set_pos(learn, 10, 126); lv_obj_set_size(learn, 145, 46);
    lv_obj_t *dict = make_btn(scr, "DICTATION", on_vocab_home_btn, 1);
    lv_obj_set_pos(dict, 165, 126); lv_obj_set_size(dict, 145, 46);
    lv_obj_set_style_bg_color(dict, lv_color_hex(0x7C3AED), 0);

    lv_obj_t *books = make_btn(scr, "BOOKS", on_vocab_home_btn, 2);
    lv_obj_set_pos(books, 10, 181); lv_obj_set_size(books, 94, 45);
    lv_obj_set_style_bg_color(books, CLR_PANEL_2, 0);
    lv_obj_t *plan = make_btn(scr, "PLAN", on_vocab_home_btn, 3);
    lv_obj_set_pos(plan, 113, 181); lv_obj_set_size(plan, 94, 45);
    lv_obj_set_style_bg_color(plan, CLR_PANEL_2, 0);
    lv_obj_t *sd = make_btn(scr, "SD", on_vocab_home_btn, 4);
    lv_obj_set_pos(sd, 216, 181); lv_obj_set_size(sd, 94, 45);
    lv_obj_set_style_bg_color(sd, CLR_PANEL_2, 0);

    refresh_vocab_dashboard();
}

static void on_vocab_local_book(lv_event_t *e)
{
    if (!click_ok()) return;
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (idx < 0 || idx >= s_vocab_local_count) return;
    s_vocab_books_notice[0] = '\0';
    esp_err_t err = vocab_open_book_async(s_vocab_local[idx].filename, VOCAB_MODE_LEARN);
    if (err != ESP_OK) snprintf(s_vocab_books_notice, sizeof(s_vocab_books_notice), "BOOK BUSY\nWait for current operation");
    refresh_vocab_download_label();
}

static void on_vocab_download(lv_event_t *e)
{
    if (!click_ok()) return;

    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (idx < 0 || idx >= vocab_catalog_count()) return;

    s_vocab_books_notice[0] = '\0';

    // Installed online books use the same non-blocking SD preparation path.
    if (vocab_catalog_is_downloaded(idx)) {
        vocab_catalog_item_t it;
        if (vocab_catalog_get(idx, &it) && it.filename) {
            esp_err_t err = vocab_open_book_async(it.filename, VOCAB_MODE_LEARN);
            if (err != ESP_OK) snprintf(s_vocab_books_notice, sizeof(s_vocab_books_notice), "BOOK BUSY\nWait for current operation");
            refresh_vocab_download_label();
        }
        return;
    }

    if (!wifi_mgr_is_connected()) {
        snprintf(s_vocab_books_notice, sizeof(s_vocab_books_notice),
                 "DOWNLOAD BLOCKED\nConnect Wi-Fi first");
        refresh_vocab_download_label();
        return;
    }

    esp_err_t err = vocab_download_catalog_async(idx);
    if (err == ESP_OK) {
        // Immediate feedback; timer keeps updating progress.
        refresh_vocab_download_label();
        return;
    }

    const sdmon_status_t *sd = sd_monitor_get_status();
    if (!sd || !sd->mounted) {
        snprintf(s_vocab_books_notice, sizeof(s_vocab_books_notice),
                 "DOWNLOAD BLOCKED\nMount SD card first");
    } else {
        snprintf(s_vocab_books_notice, sizeof(s_vocab_books_notice),
                 "DOWNLOAD BUSY\nAnother book is running");
    }
    refresh_vocab_download_label();
}

static void refresh_vocab_download_label(void)
{
    if (!s_vocab_dl_label) return;

    const vocab_download_status_t *d = vocab_download_status();
    char buf[112];
    const char *title = "Wordbook";

    if (d && d->catalog_index >= 0) {
        vocab_catalog_item_t it;
        if (vocab_catalog_get(d->catalog_index, &it) && it.title) title = it.title;
    }

    const vocab_open_status_t *op = vocab_open_status();
    if (op && op->state >= VOCAB_OPEN_PREPARING && op->state <= VOCAB_OPEN_PLANNING) {
        if (op->state == VOCAB_OPEN_IMPORTING && op->bytes_total > 0) {
            unsigned pct = (unsigned)((op->bytes_done * 100ULL) / op->bytes_total); if (pct > 100) pct = 100;
            snprintf(buf, sizeof(buf), "SD JSON IMPORT  %u%%\n%lu words converted", pct, (unsigned long)op->words_done);
        } else if (op->state == VOCAB_OPEN_INDEXING && op->bytes_total > 0) {
            unsigned pct = (unsigned)((op->bytes_done * 100ULL) / op->bytes_total); if (pct > 100) pct = 100;
            snprintf(buf, sizeof(buf), "CHECKING WORDBOOK  %u%%\n%lu words found", pct, (unsigned long)op->words_done);
        } else {
            snprintf(buf, sizeof(buf), "PREPARING WORDBOOK\n%s", op->message);
        }
        lv_obj_set_style_text_color(s_vocab_dl_label, CLR_TEXT, 0);
        lv_obj_set_style_bg_color(s_vocab_dl_label, lv_color_hex(0x17365D), 0);
        label_set_text_if_changed(s_vocab_dl_label, buf);
        return;
    }

    if (d && d->state == VOCAB_DL_DOWNLOADING) {
        if (d->bytes_total > 0) {
            unsigned pct = (unsigned)((d->bytes_done * 100ULL) / d->bytes_total);
            if (pct > 100) pct = 100;
            snprintf(buf, sizeof(buf), "%s  %u%%\n%u / %u KB",
                     title, pct,
                     (unsigned)(d->bytes_done / 1024),
                     (unsigned)(d->bytes_total / 1024));
        } else {
            snprintf(buf, sizeof(buf), "%s  DOWNLOADING\n%u KB received",
                     title, (unsigned)(d->bytes_done / 1024));
        }
        lv_obj_set_style_text_color(s_vocab_dl_label, CLR_TEXT, 0);
        lv_obj_set_style_bg_color(s_vocab_dl_label, lv_color_hex(0x17365D), 0);

    } else if (d && d->state == VOCAB_DL_CONVERTING) {
        snprintf(buf, sizeof(buf), "%s\nINSTALLING TO SD...", title);
        lv_obj_set_style_text_color(s_vocab_dl_label, CLR_TEXT, 0);
        lv_obj_set_style_bg_color(s_vocab_dl_label, lv_color_hex(0x5A4313), 0);

    } else if (s_vocab_books_notice[0]) {
        snprintf(buf, sizeof(buf), "%s", s_vocab_books_notice);
        lv_obj_set_style_text_color(s_vocab_dl_label, CLR_TEXT, 0);
        lv_obj_set_style_bg_color(s_vocab_dl_label, lv_color_hex(0x6B3F12), 0);

    } else if (d && d->state == VOCAB_DL_OK) {
        vocab_catalog_item_t it;
        const char *filename = "";
        if (d->catalog_index >= 0 &&
            vocab_catalog_get(d->catalog_index, &it) && it.filename) {
            filename = it.filename;
        }
        snprintf(buf, sizeof(buf), "DONE: %s\nSaved: %s",
                 title, filename[0] ? filename : "SD card");
        lv_obj_set_style_text_color(s_vocab_dl_label, CLR_TEXT, 0);
        lv_obj_set_style_bg_color(s_vocab_dl_label, lv_color_hex(0x0F6B38), 0);

    } else if (d && d->state == VOCAB_DL_ERROR) {
        snprintf(buf, sizeof(buf), "DOWNLOAD FAILED: %s\n%s", title, d->message);
        lv_obj_set_style_text_color(s_vocab_dl_label, CLR_TEXT, 0);
        lv_obj_set_style_bg_color(s_vocab_dl_label, lv_color_hex(0x7A1F1F), 0);

    } else {
        snprintf(buf, sizeof(buf), "BOOK DOWNLOAD STATUS\nTap [DOWNLOAD] below");
        lv_obj_set_style_text_color(s_vocab_dl_label, CLR_TEXT_DIM, 0);
        lv_obj_set_style_bg_color(s_vocab_dl_label, CLR_PANEL, 0);
    }

    label_set_text_if_changed(s_vocab_dl_label, buf);
}

static void build_vocab_books_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);

    make_title(scr, "WORDBOOKS");
    make_back_btn(scr);

    // Short, fixed-height status box. It never overlaps the scrolling area.
    s_vocab_dl_label = lv_label_create(scr);
    lv_obj_set_pos(s_vocab_dl_label, 8, 39);
    lv_obj_set_size(s_vocab_dl_label, 304, 38);
    lv_label_set_long_mode(s_vocab_dl_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_left(s_vocab_dl_label, 7, 0);
    lv_obj_set_style_pad_right(s_vocab_dl_label, 7, 0);
    lv_obj_set_style_pad_top(s_vocab_dl_label, 3, 0);
    lv_obj_set_style_pad_bottom(s_vocab_dl_label, 3, 0);
    lv_obj_set_style_radius(s_vocab_dl_label, 6, 0);
    lv_obj_set_style_border_width(s_vocab_dl_label, 1, 0);
    lv_obj_set_style_border_color(s_vocab_dl_label, lv_color_hex(0x475569), 0);
    refresh_vocab_download_label();

    lv_obj_t *sc = lv_obj_create(scr);
    lv_obj_set_pos(sc, 8, 81);
    lv_obj_set_size(sc, 304, 154);
    lv_obj_set_style_bg_color(sc, CLR_BG, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_set_style_pad_all(sc, 4, 0);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);

    int y = 0;
    lv_obj_t *section = make_label(sc, "SD BOOKS - TAP TO SELECT & LEARN", 4, y, CLR_TEXT_DIM);
    lv_obj_set_style_text_font(section, &lv_font_montserrat_14, 0);
    y += 22;

    s_vocab_local_count = vocab_list_books(s_vocab_local, VOCAB_LOCAL_MAX);
    if (s_vocab_local_count == 0) {
        make_label(sc, "No local books yet", 4, y + 4, CLR_TEXT_DIM);
        y += 28;
    } else {
        for (int i = 0; i < s_vocab_local_count; ++i) {
            char row[72];
            snprintf(row, sizeof(row), "%s%s",
                     s_vocab_local[i].selected ? "[ACTIVE] " : "",
                     s_vocab_local[i].title);

            lv_obj_t *b = make_btn(sc, row, on_vocab_local_book, i);
            lv_obj_set_pos(b, 2, y);
            lv_obj_set_size(b, 288, 38);
            lv_obj_set_style_bg_color(b,
                                      s_vocab_local[i].selected ? CLR_ACCENT : CLR_PANEL,
                                      0);
            y += 44;
        }
    }

    section = make_label(sc, "ONLINE BOOKS", 4, y + 2, CLR_TEXT_DIM);
    lv_obj_set_style_text_font(section, &lv_font_montserrat_14, 0);
    y += 27;

    for (int i = 0; i < vocab_catalog_count(); ++i) {
        vocab_catalog_item_t it;
        if (!vocab_catalog_get(i, &it)) continue;

        bool have = vocab_catalog_is_downloaded(i);
        char row[80];
        snprintf(row, sizeof(row), "%s  %s",
                 it.title, have ? "[ON SD]" : "[DOWNLOAD]");

        lv_obj_t *b = make_btn(sc, row, on_vocab_download, i);
        lv_obj_set_pos(b, 2, y);
        lv_obj_set_size(b, 288, 38);
        lv_obj_set_style_bg_color(b,
                                  have ? CLR_PANEL_2 : lv_color_hex(0x7C3AED), 0);
        y += 44;
    }
}

static void on_vocab_learn_action(lv_event_t *e)
{
    if (!click_ok()) return;
    int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (vocab_session_finished()) {
        lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_VOCAB);
        return;
    }
    if (id == 1) { // reveal
        s_vocab_revealed = true;
        refresh_vocab_learn_card();
        return;
    }
    if (id == 0) (void)vocab_mark_current(false);
    if (id == 2) (void)vocab_mark_current(true);
    if (id == 0 || id == 2) {
        (void)vocab_next();
        s_vocab_revealed = false;
        refresh_vocab_learn_card();
    }
}

static void refresh_vocab_learn_card(void)
{
    if (s_screen != SCREEN_VOCAB_LEARN || !s_vocab_word) return;
    if (vocab_session_finished()) {
        lv_label_set_text(s_vocab_word, "SESSION COMPLETE");
        lv_label_set_text(s_vocab_phonetic, "");
        lv_label_set_text(s_vocab_meaning, "Great work. Review progress is saved on the SD card.");
        lv_obj_set_style_text_font(s_vocab_meaning, &lv_font_montserrat_14, 0);
        if (s_vocab_feedback) lv_label_set_text(s_vocab_feedback, "");
        lv_label_set_text(s_vocab_progress, "DONE");
        return;
    }
    vocab_word_t w;
    if (!vocab_get_current(&w)) return;
    lv_label_set_text(s_vocab_word, w.word);
    char phbuf[128];
    format_phonetic_display(w.phonetic[0] ? w.phonetic : " ", phbuf, sizeof(phbuf));
    lv_label_set_text(s_vocab_phonetic, phbuf);
    if (s_vocab_revealed) {
        char mbuf[VOCAB_MEANING_MAX];
        format_cn_display(w.meaning, mbuf, sizeof(mbuf));
        lv_obj_set_style_text_font(s_vocab_meaning, &font_cn16, 0);
        lv_label_set_text(s_vocab_meaning, mbuf);
        lv_obj_set_style_text_color(s_vocab_meaning, CLR_TEXT, 0);
        if (s_vocab_feedback) {
            char ebuf[VOCAB_EXAMPLE_MAX];
            format_cn_display(w.example[0] ? w.example : "", ebuf, sizeof(ebuf));
            lv_obj_set_style_text_font(s_vocab_feedback, &font_cn16, 0);
            lv_label_set_text(s_vocab_feedback, ebuf);
            lv_obj_set_style_text_color(s_vocab_feedback, CLR_TEXT_DIM, 0);
        }
    } else {
        lv_obj_set_style_text_font(s_vocab_meaning, &lv_font_montserrat_14, 0);
        lv_label_set_text(s_vocab_meaning, "Tap SHOW, or answer from memory.");
        lv_obj_set_style_text_color(s_vocab_meaning, CLR_TEXT_DIM, 0);
        if (s_vocab_feedback) lv_label_set_text(s_vocab_feedback, "");
    }
    char pbuf[48];
    snprintf(pbuf, sizeof(pbuf), "%d / %d", vocab_session_position() + 1, vocab_session_count());
    lv_label_set_text(s_vocab_progress, pbuf);
}

static void build_vocab_learn_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);
    make_title(scr, "LEARN");
    make_back_btn(scr);

    lv_obj_t *card = make_card(scr, 10, 43, 300, 127);
    s_vocab_progress = make_label(card, "", 12, 8, CLR_TEXT_DIM);
    s_vocab_word = lv_label_create(card);
    lv_obj_set_size(s_vocab_word, 276, 24);
    lv_obj_set_pos(s_vocab_word, 12, 29);
    lv_obj_set_style_text_font(s_vocab_word, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_vocab_word, CLR_TEXT, 0);
    lv_obj_set_style_text_align(s_vocab_word, LV_TEXT_ALIGN_CENTER, 0);

    s_vocab_phonetic = lv_label_create(card);
    lv_obj_set_size(s_vocab_phonetic, 276, 20);
    lv_obj_set_pos(s_vocab_phonetic, 12, 53);
    lv_obj_set_style_text_color(s_vocab_phonetic, CLR_PRIMARY, 0);
    lv_obj_set_style_text_align(s_vocab_phonetic, LV_TEXT_ALIGN_CENTER, 0);

    s_vocab_meaning = lv_label_create(card);
    lv_obj_set_size(s_vocab_meaning, 276, 31);
    lv_obj_set_pos(s_vocab_meaning, 12, 72);
    lv_label_set_long_mode(s_vocab_meaning, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_vocab_meaning, LV_TEXT_ALIGN_CENTER, 0);

    // Optional example/sentence line. Remote ECDICT-derived books may leave it
    // blank, while user-created CSV books can provide richer word detail.
    s_vocab_feedback = lv_label_create(card);
    lv_obj_set_size(s_vocab_feedback, 276, 18);
    lv_obj_set_pos(s_vocab_feedback, 12, 104);
    lv_label_set_long_mode(s_vocab_feedback, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_vocab_feedback, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_vocab_feedback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_vocab_feedback, CLR_TEXT_DIM, 0);

    lv_obj_t *again = make_btn(scr, "AGAIN", on_vocab_learn_action, 0);
    lv_obj_set_pos(again, 10, 180); lv_obj_set_size(again, 94, 47);
    lv_obj_set_style_bg_color(again, CLR_ERR, 0);
    lv_obj_t *show = make_btn(scr, "SHOW", on_vocab_learn_action, 1);
    lv_obj_set_pos(show, 113, 180); lv_obj_set_size(show, 94, 47);
    lv_obj_set_style_bg_color(show, CLR_PANEL_2, 0);
    lv_obj_t *known = make_btn(scr, "KNOWN", on_vocab_learn_action, 2);
    lv_obj_set_pos(known, 216, 180); lv_obj_set_size(known, 94, 47);
    lv_obj_set_style_bg_color(known, CLR_OK, 0);

    s_vocab_revealed = false;
    refresh_vocab_learn_card();
}



// ---------------- vocabulary auxiliary screens ----------------
// Minimal stable implementations. The previous patch added declarations and
// screen routing before adding the actual LVGL builders, which caused linker
// failures. These builders keep the existing vocabulary flow intact.
static void build_vocab_dictation_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);
    make_title(scr, "Dictation - Write");
    make_back_btn(scr);

    /* Chinese meaning is the clue; the English spelling remains hidden. */
    s_hand_hint = make_label(scr, "中文提示:", 10, 40, CLR_TEXT_DIM);
    lv_obj_set_style_text_font(s_hand_hint, &font_cn16, 0);

    s_dict_prompt = lv_label_create(scr);
    lv_obj_set_pos(s_dict_prompt, 82, 39);
    lv_obj_set_size(s_dict_prompt, 228, 20);
    lv_label_set_long_mode(s_dict_prompt, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_dict_prompt, &font_cn16, 0);
    lv_obj_set_style_text_color(s_dict_prompt, CLR_TEXT, 0);

    /* The accumulated recognized word is always visible before CHECK. */
    s_dict_ta = lv_textarea_create(scr);
    lv_obj_set_pos(s_dict_ta, 10, 61);
    lv_obj_set_size(s_dict_ta, 300, 27);
    lv_textarea_set_one_line(s_dict_ta, true);
    lv_textarea_set_max_length(s_dict_ta, VOCAB_WORD_MAX - 1);
    lv_textarea_set_placeholder_text(s_dict_ta, "YOUR WORD");
    lv_textarea_set_accepted_chars(s_dict_ta, "abcdefghijklmnopqrstuvwxyz");
    lv_textarea_set_cursor_click_pos(s_dict_ta, false);
    lv_obj_set_style_text_font(s_dict_ta, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(s_dict_ta, CLR_PANEL, 0);
    lv_obj_set_style_text_color(s_dict_ta, CLR_TEXT, 0);
    lv_obj_set_style_border_color(s_dict_ta, CLR_PRIMARY, 0);
    lv_obj_set_style_radius(s_dict_ta, 7, 0);

    /*
     * Use a normal white LVGL object as the handwriting pad and draw the saved
     * stroke points in LV_EVENT_DRAW_POST. This is much more reliable than an
     * indexed 1-bit canvas on this display path, while using almost no extra RAM.
     */
    s_hand_canvas = lv_obj_create(scr);
    lv_obj_set_pos(s_hand_canvas, 10, 96);
    lv_obj_set_size(s_hand_canvas, HAND_W, HAND_H);
    lv_obj_set_style_bg_color(s_hand_canvas, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_hand_canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_hand_canvas, 1, 0);
    lv_obj_set_style_border_color(s_hand_canvas, CLR_TEXT_DIM, 0);
    lv_obj_set_style_radius(s_hand_canvas, 4, 0);
    lv_obj_set_style_pad_all(s_hand_canvas, 0, 0);
    lv_obj_clear_flag(s_hand_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_hand_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_hand_canvas, hand_draw_event, LV_EVENT_ALL, NULL);

    s_hand_clear = make_btn(scr, "CLEAR", hand_clear_event, 0);
    lv_obj_set_size(s_hand_clear, 69, 32);
    lv_obj_set_pos(s_hand_clear, 10, 176);

    s_hand_add = make_btn(scr, "ADD", hand_add_event, 0);
    lv_obj_set_size(s_hand_add, 69, 32);
    lv_obj_set_pos(s_hand_add, 87, 176);
    lv_obj_set_style_bg_color(s_hand_add, CLR_PRIMARY, 0);

    s_hand_del = make_btn(scr, "DEL", hand_del_event, 0);
    lv_obj_set_size(s_hand_del, 69, 32);
    lv_obj_set_pos(s_hand_del, 164, 176);
    lv_obj_set_style_bg_color(s_hand_del, CLR_PANEL_2, 0);

    s_dict_check = make_btn(scr, "CHECK", dict_check_event, 0);
    lv_obj_set_size(s_dict_check, 69, 32);
    lv_obj_set_pos(s_dict_check, 241, 176);
    lv_obj_set_style_bg_color(s_dict_check, CLR_OK, 0);

    s_dict_feedback = lv_label_create(scr);
    lv_obj_set_pos(s_dict_feedback, 10, 213);
    lv_obj_set_size(s_dict_feedback, 300, 22);
    lv_label_set_long_mode(s_dict_feedback, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_dict_feedback, &font_cn16, 0);
    lv_obj_set_style_text_align(s_dict_feedback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_dict_feedback, CLR_TEXT_DIM, 0);

    s_dict_checked = false;
    hand_clear_pad();
    refresh_vocab_dictation();
}

static void build_vocab_plan_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);
    make_title(scr, "Study Plan");
    make_back_btn(scr);

    lv_obj_t *title = make_label(scr, "每日学习目标", 20, 49, CLR_TEXT_DIM);
    lv_obj_set_size(title, 280, 20);
    lv_obj_set_style_text_font(title, &font_cn16, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    s_plan_value = make_label(scr, "", 20, 76, CLR_TEXT);
    lv_obj_set_size(s_plan_value, 280, 24);
    lv_obj_set_style_text_font(s_plan_value, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_plan_value, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *minus = make_btn(scr, "-5", plan_action_event, 0);
    lv_obj_set_pos(minus, 20, 111);
    lv_obj_set_size(minus, 82, 42);
    lv_obj_set_style_bg_color(minus, CLR_PANEL_2, 0);

    lv_obj_t *save = make_btn(scr, "SAVE", plan_action_event, 1);
    lv_obj_set_pos(save, 119, 111);
    lv_obj_set_size(save, 82, 42);
    lv_obj_set_style_bg_color(save, CLR_OK, 0);

    lv_obj_t *plus = make_btn(scr, "+5", plan_action_event, 2);
    lv_obj_set_pos(plus, 218, 111);
    lv_obj_set_size(plus, 82, 42);

    s_plan_stats = lv_label_create(scr);
    lv_obj_set_pos(s_plan_stats, 20, 166);
    lv_obj_set_size(s_plan_stats, 280, 58);
    lv_label_set_long_mode(s_plan_stats, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_plan_stats, &font_cn16, 0);
    lv_obj_set_style_text_align(s_plan_stats, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_plan_stats, CLR_TEXT_DIM, 0);

    s_plan_edit_target = vocab_get_daily_target();
    s_plan_notice[0] = '\0';
    refresh_plan_labels();
}

static void refresh_vocab_dictation(void)
{
    if (s_screen != SCREEN_VOCAB_DICTATION) return;

    if (vocab_session_finished()) {
        if (s_dict_prompt) lv_label_set_text(s_dict_prompt, "本轮完成");
        if (s_dict_feedback) {
            lv_label_set_text(s_dict_feedback, "学习进度已保存 点击返回继续");
            lv_obj_set_style_text_color(s_dict_feedback, CLR_OK, 0);
        }
        if (s_dict_check) set_btn_text(s_dict_check, "DONE");
        return;
    }

    vocab_word_t w;
    if (!vocab_get_current(&w)) return;

    if (s_dict_prompt) {
        char meaning[VOCAB_MEANING_MAX];
        format_cn_display(w.meaning[0] ? w.meaning : "无中文释义", meaning, sizeof(meaning));
        lv_label_set_text(s_dict_prompt, meaning);
    }

    if (s_dict_check) set_btn_text(s_dict_check, s_dict_checked ? "NEXT" : "CHECK");

    if (!s_dict_checked && s_dict_feedback) {
        lv_label_set_text(s_dict_feedback, "可一次写多个大写字母 字母间留空 点 ADD");
        lv_obj_set_style_text_color(s_dict_feedback, CLR_TEXT_DIM, 0);
    }
}

static void hand_clear_pad(void)
{
    s_hand_point_count = 0;
    s_hand_pen_down = false;
    if (s_hand_canvas) {
        lv_obj_invalidate(s_hand_canvas);
    }
}

static void hand_clear_event(lv_event_t *e)
{
    (void)e;
    if (!click_ok()) return;
    if (s_dict_checked) {
        if (s_dict_feedback) lv_label_set_text(s_dict_feedback, "已经 CHECK 请点 NEXT 进入下一个单词");
        return;
    }
    hand_clear_pad();
    if (s_dict_feedback) {
        lv_label_set_text(s_dict_feedback, "手写区已清空 已识别单词保留");
        lv_obj_set_style_text_color(s_dict_feedback, CLR_TEXT_DIM, 0);
    }
}

static void hand_del_event(lv_event_t *e)
{
    (void)e;
    if (!click_ok()) return;
    if (!s_dict_ta) return;
    if (s_dict_checked) {
        if (s_dict_feedback) lv_label_set_text(s_dict_feedback, "已经 CHECK 请点 NEXT 进入下一个单词");
        return;
    }
    lv_textarea_del_char(s_dict_ta);
    if (s_dict_feedback) {
        char msg[128];
        snprintf(msg, sizeof(msg), "已删最后一个字母 当前: %.47s", lv_textarea_get_text(s_dict_ta));
        lv_label_set_text(s_dict_feedback, msg);
        lv_obj_set_style_text_color(s_dict_feedback, CLR_TEXT_DIM, 0);
    }
}

/*
 * Offline uppercase handwriting classifier.
 *
 * Compared with the previous 5x7-only matcher, this version rasterizes each
 * written letter into a 9x13 normalized grid, compares both pixel geometry and
 * row/column profiles, and segments multiple letters by horizontal whitespace.
 * It remains intentionally lightweight so it can run locally on the ESP32-S3.
 */
static const uint8_t s_hand_templates[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0F,0x10,0x10,0x17,0x11,0x11,0x0E}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, /* I */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x11,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
};

typedef struct {
    uint8_t cell[HAND_GRID_H][HAND_GRID_W];
    int span_x;
    int span_y;
} hand_grid_t;

static void hand_ngrid_mark(hand_grid_t *g, int x, int y)
{
    if (!g || x < 0 || x >= HAND_GRID_W || y < 0 || y >= HAND_GRID_H) return;
    g->cell[y][x] = 1;
}

static void hand_ngrid_line(hand_grid_t *g, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        hand_ngrid_mark(g, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static bool hand_template_cell(int idx, int x, int y)
{
    return (s_hand_templates[idx][y] & (1U << (4 - x))) != 0;
}

static void hand_build_template(int idx, hand_grid_t *g)
{
    memset(g, 0, sizeof(*g));
    g->span_x = HAND_GRID_W - 1;
    g->span_y = HAND_GRID_H - 1;

    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (!hand_template_cell(idx, x, y)) continue;
            int gx = x * 2;
            int gy = y * 2;
            hand_ngrid_mark(g, gx, gy);

            if (x + 1 < 5 && hand_template_cell(idx, x + 1, y))
                hand_ngrid_line(g, gx, gy, gx + 2, gy);
            if (y + 1 < 7 && hand_template_cell(idx, x, y + 1))
                hand_ngrid_line(g, gx, gy, gx, gy + 2);
            if (y + 1 < 7 && x + 1 < 5 && hand_template_cell(idx, x + 1, y + 1))
                hand_ngrid_line(g, gx, gy, gx + 2, gy + 2);
            if (y + 1 < 7 && x > 0 && hand_template_cell(idx, x - 1, y + 1))
                hand_ngrid_line(g, gx, gy, gx - 2, gy + 2);
        }
    }
}

static bool hand_build_input(int x0, int x1, hand_grid_t *g)
{
    int min_x = HAND_W, min_y = HAND_H, max_x = -1, max_y = -1;
    int real_points = 0;
    memset(g, 0, sizeof(*g));

    for (int i = 0; i < s_hand_point_count; ++i) {
        lv_point_t p = s_hand_points[i];
        if (p.x == HAND_BREAK_COORD || p.x < x0 || p.x > x1) continue;
        if (p.x < min_x) min_x = p.x;
        if (p.x > max_x) max_x = p.x;
        if (p.y < min_y) min_y = p.y;
        if (p.y > max_y) max_y = p.y;
        real_points++;
    }
    if (real_points < 3 || max_x < min_x || max_y < min_y) return false;

    int span_x = max_x - min_x;
    int span_y = max_y - min_y;
    if (span_x < 1) span_x = 1;
    if (span_y < 1) span_y = 1;
    g->span_x = span_x;
    g->span_y = span_y;

    bool have_prev = false;
    int prev_x = 0, prev_y = 0;
    for (int i = 0; i < s_hand_point_count; ++i) {
        lv_point_t p = s_hand_points[i];
        if (p.x == HAND_BREAK_COORD) {
            have_prev = false;
            continue;
        }
        if (p.x < x0 || p.x > x1) {
            have_prev = false;
            continue;
        }

        int gx = ((p.x - min_x) * (HAND_GRID_W - 1) + span_x / 2) / span_x;
        int gy = ((p.y - min_y) * (HAND_GRID_H - 1) + span_y / 2) / span_y;
        if (gx < 0) gx = 0;
        if (gx >= HAND_GRID_W) gx = HAND_GRID_W - 1;
        if (gy < 0) gy = 0;
        if (gy >= HAND_GRID_H) gy = HAND_GRID_H - 1;

        if (have_prev) hand_ngrid_line(g, prev_x, prev_y, gx, gy);
        else hand_ngrid_mark(g, gx, gy);
        prev_x = gx;
        prev_y = gy;
        have_prev = true;
    }
    return true;
}

static int hand_grid_count(const hand_grid_t *g)
{
    int n = 0;
    for (int y = 0; y < HAND_GRID_H; ++y)
        for (int x = 0; x < HAND_GRID_W; ++x)
            n += g->cell[y][x] ? 1 : 0;
    return n;
}

static int hand_grid_distance(const hand_grid_t *a, const hand_grid_t *b)
{
    int ac = hand_grid_count(a);
    int bc = hand_grid_count(b);
    if (ac <= 0 || bc <= 0) return 100000;

    int ab = 0;
    for (int ay = 0; ay < HAND_GRID_H; ++ay) {
        for (int ax = 0; ax < HAND_GRID_W; ++ax) {
            if (!a->cell[ay][ax]) continue;
            int best = 99;
            for (int by = 0; by < HAND_GRID_H; ++by) {
                for (int bx = 0; bx < HAND_GRID_W; ++bx) {
                    if (!b->cell[by][bx]) continue;
                    int d = abs(ax - bx) + abs(ay - by);
                    if (d < best) best = d;
                }
            }
            ab += best;
        }
    }

    int ba = 0;
    for (int by = 0; by < HAND_GRID_H; ++by) {
        for (int bx = 0; bx < HAND_GRID_W; ++bx) {
            if (!b->cell[by][bx]) continue;
            int best = 99;
            for (int ay = 0; ay < HAND_GRID_H; ++ay) {
                for (int ax = 0; ax < HAND_GRID_W; ++ax) {
                    if (!a->cell[ay][ax]) continue;
                    int d = abs(ax - bx) + abs(ay - by);
                    if (d < best) best = d;
                }
            }
            ba += best;
        }
    }

    int cost = (ab * 100) / ac + (ba * 100) / bc;

    int profile = 0;
    for (int y = 0; y < HAND_GRID_H; ++y) {
        int ar = 0, br = 0;
        for (int x = 0; x < HAND_GRID_W; ++x) {
            ar += a->cell[y][x] ? 1 : 0;
            br += b->cell[y][x] ? 1 : 0;
        }
        profile += abs((ar * 100) / ac - (br * 100) / bc);
    }
    for (int x = 0; x < HAND_GRID_W; ++x) {
        int av = 0, bv = 0;
        for (int y = 0; y < HAND_GRID_H; ++y) {
            av += a->cell[y][x] ? 1 : 0;
            bv += b->cell[y][x] ? 1 : 0;
        }
        profile += abs((av * 100) / ac - (bv * 100) / bc);
    }
    cost += profile / 4;
    cost += abs(ac - bc) * 2;
    return cost;
}

static bool hand_grid_has(const hand_grid_t *g, int x0, int x1, int y0, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= HAND_GRID_W) x1 = HAND_GRID_W - 1;
    if (y1 >= HAND_GRID_H) y1 = HAND_GRID_H - 1;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (g->cell[y][x]) return true;
    return false;
}

static int hand_shape_adjust(int idx, const hand_grid_t *g)
{
    int adjust = 0;

    /* A: apex near the top, two lower legs, and a middle cross stroke. */
    if (idx == 0) {
        bool apex = hand_grid_has(g, 3, 5, 0, 3);
        bool left_leg = hand_grid_has(g, 0, 3, 7, 12);
        bool right_leg = hand_grid_has(g, 5, 8, 7, 12);
        bool cross = false;
        for (int y = 4; y <= 9; ++y) {
            int minx = HAND_GRID_W, maxx = -1, n = 0;
            for (int x = 0; x < HAND_GRID_W; ++x) {
                if (!g->cell[y][x]) continue;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                n++;
            }
            if (n >= 3 && minx <= 3 && maxx >= 5) { cross = true; break; }
        }
        if (apex && left_leg && right_leg && cross) adjust -= 180;
    }

    /* A very narrow tall glyph is much more likely to be I than H/T/L. */
    if (idx == 8 && g->span_y > 0 && g->span_x * 100 / g->span_y < 35) adjust -= 120;
    return adjust;
}

static char hand_recognize_one(int x0, int x1, int *confidence)
{
    hand_grid_t input;
    if (!hand_build_input(x0, x1, &input)) return '\0';

    int best_idx = -1;
    int best = 100000;
    int second = 100000;
    for (int i = 0; i < 26; ++i) {
        hand_grid_t templ;
        hand_build_template(i, &templ);
        int d = hand_grid_distance(&input, &templ) + hand_shape_adjust(i, &input);
        if (d < 0) d = 0;
        if (d < best) {
            second = best;
            best = d;
            best_idx = i;
        } else if (d < second) {
            second = d;
        }
    }
    if (best_idx < 0) return '\0';

    if (confidence) {
        int gap = second > best ? second - best : 0;
        int c = 96 - best / 12 + gap / 5;
        if (c < 25) c = 25;
        if (c > 99) c = 99;
        *confidence = c;
    }
    return (char)('A' + best_idx);
}

static int hand_segment_ranges(int ranges[][2], int max_ranges)
{
    uint8_t occupied[HAND_W];
    memset(occupied, 0, sizeof(occupied));

    bool have_prev = false;
    lv_point_t prev = {0, 0};
    for (int i = 0; i < s_hand_point_count; ++i) {
        lv_point_t p = s_hand_points[i];
        if (p.x == HAND_BREAK_COORD) {
            have_prev = false;
            continue;
        }
        if (p.x < 0 || p.x >= HAND_W) continue;
        occupied[p.x] = 1;
        if (have_prev) {
            int a = prev.x < p.x ? prev.x : p.x;
            int b = prev.x > p.x ? prev.x : p.x;
            if (a < 0) a = 0;
            if (b >= HAND_W) b = HAND_W - 1;
            for (int x = a; x <= b; ++x) occupied[x] = 1;
        }
        prev = p;
        have_prev = true;
    }

    int n = 0;
    int start = -1;
    int last = -1;
    int gap = 0;
    for (int x = 0; x < HAND_W; ++x) {
        if (occupied[x]) {
            if (start < 0) start = x;
            last = x;
            gap = 0;
        } else if (start >= 0) {
            gap++;
            if (gap >= HAND_SEG_GAP) {
                if (n < max_ranges) {
                    ranges[n][0] = start;
                    ranges[n][1] = last;
                    n++;
                }
                start = -1;
                last = -1;
                gap = 0;
            }
        }
    }
    if (start >= 0 && n < max_ranges) {
        ranges[n][0] = start;
        ranges[n][1] = last;
        n++;
    }
    return n;
}

static int hand_recognize_segments(char *out, size_t out_sz, int *avg_confidence)
{
    if (!out || out_sz < 2) return 0;
    out[0] = '\0';

    int ranges[HAND_MAX_SEGMENTS][2];
    int segments = hand_segment_ranges(ranges, HAND_MAX_SEGMENTS);
    if (segments <= 0) return 0;

    int total_conf = 0;
    int written = 0;
    for (int i = 0; i < segments && written + 1 < (int)out_sz; ++i) {
        int conf = 0;
        char c = hand_recognize_one(ranges[i][0], ranges[i][1], &conf);
        if (!c) continue;
        out[written++] = c;
        total_conf += conf;
    }
    out[written] = '\0';
    if (avg_confidence) *avg_confidence = written > 0 ? total_conf / written : 0;
    return written;
}

static int hand_commit_pad(bool show_feedback)
{
    if (!s_dict_ta || s_hand_point_count <= 0) return 0;

    char letters[HAND_MAX_SEGMENTS + 1];
    int confidence = 0;
    int n = hand_recognize_segments(letters, sizeof(letters), &confidence);
    if (n <= 0) {
        if (show_feedback && s_dict_feedback) {
            lv_label_set_text(s_dict_feedback, "\u8bf7\u5148\u5728\u767d\u8272\u624b\u5199\u533a\u5199\u5927\u5199\u82f1\u6587\u5b57\u6bcd");
            lv_obj_set_style_text_color(s_dict_feedback, CLR_WARN, 0);
        }
        return 0;
    }

    size_t len = strlen(lv_textarea_get_text(s_dict_ta));
    int added = 0;
    for (int i = 0; i < n && len < VOCAB_WORD_MAX - 1; ++i, ++len) {
        lv_textarea_add_char(s_dict_ta, (uint32_t)(letters[i] - 'A' + 'a'));
        added++;
    }

    if (added > 0) hand_clear_pad();

    if (show_feedback && s_dict_feedback) {
        char msg[160];
        if (added <= 0) {
            lv_label_set_text(s_dict_feedback, "\u5355\u8bcd\u592a\u957f \u4e0d\u80fd\u7ee7\u7eed\u6dfb\u52a0");
            lv_obj_set_style_text_color(s_dict_feedback, CLR_WARN, 0);
        } else {
            snprintf(msg, sizeof(msg), "\u8bc6\u522b: %.12s  \u5f53\u524d: %.47s", letters, lv_textarea_get_text(s_dict_ta));
            lv_label_set_text(s_dict_feedback, msg);
            lv_obj_set_style_text_color(s_dict_feedback, confidence >= 60 ? CLR_OK : CLR_WARN, 0);
        }
    }
    return added;
}

static void hand_add_event(lv_event_t *e)
{
    (void)e;
    if (!click_ok()) return;
    if (!s_dict_ta) return;
    if (s_dict_checked) {
        if (s_dict_feedback) lv_label_set_text(s_dict_feedback, "\u5df2\u7ecf CHECK \u8bf7\u70b9 NEXT \u8fdb\u5165\u4e0b\u4e00\u4e2a\u5355\u8bcd");
        return;
    }
    (void)hand_commit_pad(true);
}

static void dict_check_event(lv_event_t *e)
{
    (void)e;
    if (!click_ok()) return;

    if (vocab_session_finished()) {
        lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_VOCAB);
        return;
    }

    if (s_dict_checked) {
        (void)vocab_next();
        s_dict_checked = false;
        if (s_dict_ta) lv_textarea_set_text(s_dict_ta, "");
        hand_clear_pad();
        refresh_vocab_dictation();
        return;
    }

    if (s_hand_point_count > 0) (void)hand_commit_pad(false);

    const char *answer = s_dict_ta ? lv_textarea_get_text(s_dict_ta) : "";
    if (!answer || !answer[0]) {
        if (s_dict_feedback) {
            lv_label_set_text(s_dict_feedback, "还没有输入单词 可直接手写后点 CHECK");
            lv_obj_set_style_text_color(s_dict_feedback, CLR_WARN, 0);
        }
        return;
    }

    vocab_word_t w;
    if (!vocab_get_current(&w)) return;
    bool correct = vocab_check_dictation(answer);
    (void)vocab_mark_current(correct);
    s_dict_checked = true;

    if (s_dict_feedback) {
        char msg[192];
        if (correct) {
            snprintf(msg, sizeof(msg), "对了 你写的是: %.47s", answer);
            lv_obj_set_style_text_color(s_dict_feedback, CLR_OK, 0);
        } else {
            snprintf(msg, sizeof(msg), "不对 你写的是: %.47s  答案: %.47s", answer, w.word);
            lv_obj_set_style_text_color(s_dict_feedback, CLR_ERR, 0);
        }
        lv_label_set_text(s_dict_feedback, msg);
    }
    if (s_dict_check) set_btn_text(s_dict_check, "NEXT");
}

static void hand_draw_event(lv_event_t *e)
{
    if (!s_hand_canvas) return;
    lv_event_code_t code = lv_event_get_code(e);

    /* Draw the stored stroke points over the normal white pad. */
    if (code == LV_EVENT_DRAW_POST) {
        lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
        if (!draw_ctx) return;

        lv_area_t area;
        lv_obj_get_coords(s_hand_canvas, &area);
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = lv_color_black();
        dsc.width = 3;
        dsc.round_start = 1;
        dsc.round_end = 1;

        bool have_prev = false;
        lv_point_t prev = {0, 0};
        for (int i = 0; i < s_hand_point_count; ++i) {
            lv_point_t p = s_hand_points[i];
            if (p.x == HAND_BREAK_COORD) {
                have_prev = false;
                continue;
            }

            lv_point_t cur = {
                .x = (lv_coord_t)(area.x1 + p.x),
                .y = (lv_coord_t)(area.y1 + p.y),
            };
            if (have_prev) {
                lv_draw_line(draw_ctx, &dsc, &prev, &cur);
            }
            prev = cur;
            have_prev = true;
        }
        return;
    }

    if (s_dict_checked) return;
    if (code != LV_EVENT_PRESSING && code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED) return;

    if (code == LV_EVENT_RELEASED) {
        if (s_hand_pen_down && s_hand_point_count < HAND_MAX_POINTS) {
            s_hand_points[s_hand_point_count].x = HAND_BREAK_COORD;
            s_hand_points[s_hand_point_count].y = HAND_BREAK_COORD;
            s_hand_point_count++;
        }
        s_hand_pen_down = false;
        lv_obj_invalidate(s_hand_canvas);
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t area;
    lv_obj_get_coords(s_hand_canvas, &area);
    p.x -= area.x1;
    p.y -= area.y1;

    if (p.x < 0 || p.y < 0 || p.x >= HAND_W || p.y >= HAND_H) return;

    s_hand_pen_down = true;
    if (s_hand_point_count < HAND_MAX_POINTS) {
        s_hand_points[s_hand_point_count++] = p;
    }
    lv_obj_invalidate(s_hand_canvas);
}

static void plan_action_event(lv_event_t *e)
{
    if (!click_ok()) return;
    int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));

    if (id == 0) {
        s_plan_edit_target = s_plan_edit_target <= 5 ? 5 : (uint16_t)(s_plan_edit_target - 5);
        s_plan_notice[0] = '\0';
    } else if (id == 2) {
        s_plan_edit_target = s_plan_edit_target >= 100 ? 100 : (uint16_t)(s_plan_edit_target + 5);
        s_plan_notice[0] = '\0';
    } else if (id == 1) {
        esp_err_t err = vocab_set_daily_target(s_plan_edit_target);
        if (err == ESP_OK) {
            snprintf(s_plan_notice, sizeof(s_plan_notice), "已保存: 每天 %u 个单词", (unsigned)s_plan_edit_target);
        } else {
            snprintf(s_plan_notice, sizeof(s_plan_notice), "保存失败: %.31s", esp_err_to_name(err));
        }
    }
    refresh_plan_labels();
}

// ---------------- 2048 screen ----------------
#define GAME2048_X 12
#define GAME2048_Y 52
#define GAME2048_CELL 45

static void on_2048_move_dir(int dir)
{
    if (game2048_move(&s_game2048, dir)) {
        refresh_2048();
    }
}

static void on_2048_key(lv_event_t *e)
{
    if (!click_ok()) return;
    lv_obj_t *target = lv_event_get_target(e);
    on_2048_move_dir((int)(intptr_t)lv_obj_get_user_data(target));
}

static void on_2048_touch(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_2048_touch_start = p;
        s_2048_touch_active = true;
    } else if (code == LV_EVENT_RELEASED && s_2048_touch_active) {
        int dx = p.x - s_2048_touch_start.x;
        int dy = p.y - s_2048_touch_start.y;
        s_2048_touch_active = false;
        if (abs(dx) < 15 && abs(dy) < 15) return;
        if (abs(dx) > abs(dy)) on_2048_move_dir(dx > 0 ? 3 : 2);
        else on_2048_move_dir(dy > 0 ? 1 : 0);
    }
}

static void build_2048_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);
    make_title(scr, "2048");
    make_back_btn(scr);

    s_2048_score = make_label(scr, "Score: 0", 12, 34, CLR_TEXT);
    s_2048_status = make_label(scr, "Swipe to move", 150, 34, CLR_TEXT_DIM);

    lv_obj_t *board = lv_obj_create(scr);
    lv_obj_set_size(board, GAME2048_CELL * 4 + 8, GAME2048_CELL * 4 + 8);
    lv_obj_set_pos(board, GAME2048_X - 4, GAME2048_Y - 4);
    lv_obj_set_style_bg_color(board, CLR_PANEL, 0);
    lv_obj_set_style_radius(board, 8, 0);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(board, on_2048_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(board, on_2048_touch, LV_EVENT_RELEASED, NULL);

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            lv_obj_t *c = lv_obj_create(scr);
            lv_obj_set_size(c, GAME2048_CELL - 3, GAME2048_CELL - 3);
            lv_obj_set_pos(c, GAME2048_X + x * GAME2048_CELL, GAME2048_Y + y * GAME2048_CELL);
            lv_obj_set_style_bg_color(c, CLR_PANEL_2, 0);
            lv_obj_set_style_radius(c, 6, 0);
            lv_obj_set_style_border_width(c, 0, 0);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(c, on_2048_touch, LV_EVENT_PRESSED, NULL);
            lv_obj_add_event_cb(c, on_2048_touch, LV_EVENT_RELEASED, NULL);
            s_2048_cells[y][x] = lv_label_create(c);
            lv_obj_center(s_2048_cells[y][x]);
            lv_obj_set_style_text_color(s_2048_cells[y][x], CLR_TEXT, 0);
            lv_obj_set_style_text_font(s_2048_cells[y][x], &lv_font_montserrat_16, 0);
        }
    }

    const char *keys[] = {"UP", "LEFT", "DOWN", "RIGHT"};
    int dirs[] = {0, 2, 1, 3};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = make_btn(scr, keys[i], on_2048_key, dirs[i]);
        lv_obj_set_size(b, 65, 26);
        lv_obj_set_pos(b, 5 + i * 78, 235);
    }
    refresh_2048();
}

static void refresh_2048(void)
{
    if (s_2048_score) {
        char b[32];
        snprintf(b, sizeof(b), "Score: %d", s_game2048.score);
        lv_label_set_text(s_2048_score, b);
    }
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            char t[12];
            if (s_game2048.tile[y][x]) snprintf(t, sizeof(t), "%d", s_game2048.tile[y][x]);
            else strcpy(t, "");
            lv_label_set_text(s_2048_cells[y][x], t);
        }
    }
}

// ---------------- FLAPPY screen ----------------
static void on_flappy_tap(lv_event_t *e)
{
    (void)e;
    if (lv_tick_get() - s_screen_enter_ms < UI_SCREEN_GUARD_MS) return;

    if (s_flappy.game_over) {
        flappy_init(&s_flappy);
    }
    flappy_flap(&s_flappy);
    refresh_flappy();
}

static void build_flappy_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);
    make_title(scr, "FLAPPY BIRD");
    make_back_btn(scr);

    s_flappy_score = make_label(scr, "Score: 0", 12, 35, CLR_TEXT);
    s_flappy_status = make_label(scr, "Tap to fly", 156, 35, CLR_TEXT_DIM);
    lv_obj_set_size(s_flappy_status, 150, 18);
    lv_obj_set_style_text_align(s_flappy_status, LV_TEXT_ALIGN_RIGHT, 0);

    s_flappy_board = lv_obj_create(scr);
    lv_obj_set_pos(s_flappy_board, 10, 54);
    lv_obj_set_size(s_flappy_board, FLAPPY_WORLD_W, FLAPPY_WORLD_H);
    lv_obj_set_style_bg_color(s_flappy_board, lv_color_hex(0xDDF4FF), 0);
    lv_obj_set_style_bg_opa(s_flappy_board, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_flappy_board, 1, 0);
    lv_obj_set_style_border_color(s_flappy_board, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_radius(s_flappy_board, 6, 0);
    lv_obj_set_style_pad_all(s_flappy_board, 0, 0);
    lv_obj_clear_flag(s_flappy_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_flappy_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_flappy_board, on_flappy_tap, LV_EVENT_PRESSED, NULL);

    for (int i = 0; i < FLAPPY_PIPE_COUNT; ++i) {
        s_flappy_pipe_top[i] = lv_obj_create(s_flappy_board);
        lv_obj_set_style_bg_color(s_flappy_pipe_top[i], lv_color_hex(0x22C55E), 0);
        lv_obj_set_style_border_width(s_flappy_pipe_top[i], 0, 0);
        lv_obj_set_style_radius(s_flappy_pipe_top[i], 3, 0);
        lv_obj_set_style_pad_all(s_flappy_pipe_top[i], 0, 0);
        lv_obj_clear_flag(s_flappy_pipe_top[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        s_flappy_pipe_bottom[i] = lv_obj_create(s_flappy_board);
        lv_obj_set_style_bg_color(s_flappy_pipe_bottom[i], lv_color_hex(0x16A34A), 0);
        lv_obj_set_style_border_width(s_flappy_pipe_bottom[i], 0, 0);
        lv_obj_set_style_radius(s_flappy_pipe_bottom[i], 3, 0);
        lv_obj_set_style_pad_all(s_flappy_pipe_bottom[i], 0, 0);
        lv_obj_clear_flag(s_flappy_pipe_bottom[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    s_flappy_bird = lv_obj_create(s_flappy_board);
    lv_obj_set_size(s_flappy_bird, FLAPPY_BIRD_W, FLAPPY_BIRD_H);
    lv_obj_set_style_bg_color(s_flappy_bird, lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_border_width(s_flappy_bird, 1, 0);
    lv_obj_set_style_border_color(s_flappy_bird, lv_color_hex(0xA16207), 0);
    lv_obj_set_style_radius(s_flappy_bird, 7, 0);
    lv_obj_set_style_pad_all(s_flappy_bird, 0, 0);
    lv_obj_clear_flag(s_flappy_bird, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *eye = lv_obj_create(s_flappy_bird);
    lv_obj_set_size(eye, 4, 4);
    lv_obj_set_pos(eye, 11, 2);
    lv_obj_set_style_bg_color(eye, lv_color_black(), 0);
    lv_obj_set_style_border_width(eye, 0, 0);
    lv_obj_set_style_radius(eye, 2, 0);
    lv_obj_set_style_pad_all(eye, 0, 0);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    flappy_init(&s_flappy);
    refresh_flappy();

    if (s_flappy_timer) lv_timer_del(s_flappy_timer);
    s_flappy_timer = lv_timer_create(flappy_timer_cb, 55, NULL);
}

static void refresh_flappy(void)
{
    if (s_screen != SCREEN_FLAPPY || !s_flappy_board) return;

    if (s_flappy_bird) {
        lv_obj_set_pos(s_flappy_bird, FLAPPY_BIRD_X, s_flappy.bird_y);
    }

    for (int i = 0; i < FLAPPY_PIPE_COUNT; ++i) {
        int top_h = s_flappy.gap_y[i];
        int bottom_y = s_flappy.gap_y[i] + FLAPPY_GAP_H;
        int bottom_h = FLAPPY_WORLD_H - bottom_y;
        if (top_h < 1) top_h = 1;
        if (bottom_h < 1) bottom_h = 1;

        if (s_flappy_pipe_top[i]) {
            lv_obj_set_pos(s_flappy_pipe_top[i], s_flappy.pipe_x[i], 0);
            lv_obj_set_size(s_flappy_pipe_top[i], FLAPPY_PIPE_W, top_h);
        }
        if (s_flappy_pipe_bottom[i]) {
            lv_obj_set_pos(s_flappy_pipe_bottom[i], s_flappy.pipe_x[i], bottom_y);
            lv_obj_set_size(s_flappy_pipe_bottom[i], FLAPPY_PIPE_W, bottom_h);
        }
    }

    if (s_flappy_score) {
        char score[32];
        snprintf(score, sizeof(score), "Score: %d", s_flappy.score);
        lv_label_set_text(s_flappy_score, score);
    }
    if (s_flappy_status) {
        if (s_flappy.game_over) {
            lv_label_set_text(s_flappy_status, "GAME OVER - tap restart");
            lv_obj_set_style_text_color(s_flappy_status, CLR_ERR, 0);
        } else if (!s_flappy.started) {
            lv_label_set_text(s_flappy_status, "Tap to fly");
            lv_obj_set_style_text_color(s_flappy_status, CLR_TEXT_DIM, 0);
        } else {
            lv_label_set_text(s_flappy_status, "Keep flying!");
            lv_obj_set_style_text_color(s_flappy_status, CLR_OK, 0);
        }
    }
}

static void flappy_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_screen != SCREEN_FLAPPY) return;
    flappy_step(&s_flappy);
    refresh_flappy();
}

static void refresh_plan_labels(void)
{
    if (s_plan_value) {
        char value[48];
        snprintf(value, sizeof(value), "%u WORDS / DAY", (unsigned)s_plan_edit_target);
        lv_label_set_text(s_plan_value, value);
    }

    if (s_plan_stats) {
        vocab_stats_t st;
        char stats[220];
        uint16_t saved = vocab_get_daily_target();

        if (vocab_get_stats(&st)) {
            snprintf(stats, sizeof(stats),
                     "今日完成: %u / %u    复习: %lu\n当前已保存目标: %u / 天%s%.63s",
                     (unsigned)st.today_done, (unsigned)st.daily_target,
                     (unsigned long)st.due_words, (unsigned)saved,
                     s_plan_notice[0] ? "\n" : "", s_plan_notice);
        } else {
            snprintf(stats, sizeof(stats),
                     "范围: 5 - 100  每次加减 5 个\n当前已保存目标: %u / 天%s%.63s",
                     (unsigned)saved, s_plan_notice[0] ? "\n" : "", s_plan_notice);
        }
        lv_label_set_text(s_plan_stats, stats);
    }
}

// ---------------- public API ----------------
// All public entry points are thread-safe: they post a command to the LVGL
// task queue. Actual LVGL work runs in ui_process_cmd() on the LVGL thread.

static void do_draw_all(void)
{
    switch (s_screen) {
        case SCREEN_MENU:    build_menu_screen(); break;
        case SCREEN_SD:      build_sd_screen(); break;
        case SCREEN_SYSINFO: build_sysinfo_screen(); break;
        case SCREEN_WIFI:    build_wifi_screen(); break;
        case SCREEN_SUDOKU:  build_sudoku_screen(); break;
        case SCREEN_2048: build_2048_screen(); break;
        case SCREEN_FLAPPY: build_flappy_screen(); break;
        case SCREEN_PCMON: build_pcmon_screen(); break;
        case SCREEN_INVENTORY: build_inventory_screen(); break;
        case SCREEN_VOCAB: build_vocab_screen(); break;
        case SCREEN_VOCAB_BOOKS: build_vocab_books_screen(); break;
        case SCREEN_VOCAB_LEARN: build_vocab_learn_screen(); break;
        case SCREEN_VOCAB_DICTATION: build_vocab_dictation_screen(); break;
        case SCREEN_VOCAB_PLAN: build_vocab_plan_screen(); break;
        default:             build_menu_screen(); break;
    }
}

static void do_update_status(void)
{
    switch (s_screen) {
        case SCREEN_SD:      refresh_sd_labels(); break;
        case SCREEN_SYSINFO: refresh_sysinfo_labels(); break;
        case SCREEN_WIFI:    refresh_wifi_labels(); break;
        case SCREEN_MENU:    refresh_menu_bottom(); break;
        case SCREEN_PCMON:   refresh_pcmon_screen(); break;
        case SCREEN_VOCAB:   refresh_vocab_dashboard(); break;
        case SCREEN_VOCAB_BOOKS: refresh_vocab_download_label(); break;
        case SCREEN_VOCAB_PLAN: refresh_plan_labels(); break;
        default: break;
    }
}

static void do_set_screen(ui_screen_t scr)
{
    if (s_flappy_timer) {
        lv_timer_del(s_flappy_timer);
        s_flappy_timer = NULL;
    }
    // Any pending calibration-confirm overlay must not survive a screen switch.
    if (s_cal_layer) {
        lv_obj_del(s_cal_layer);
        s_cal_layer = NULL;
    }
    if (scr != SCREEN_VOCAB_BOOKS) s_vocab_books_notice[0] = '\0';
    s_screen = scr;
    s_wifi_sel = -1;
    s_last_wifi_state = (wifi_state_t)-1;
    s_screen_enter_ms = lv_tick_get();
    s_last_click_ms = 0;

    // Screen rebuilds delete all child objects. Clear cross-screen widget
    // pointers so periodic refresh callbacks never touch a stale LVGL object.
    s_vocab_status = NULL;
    s_vocab_dl_label = NULL;
    s_vocab_word = NULL;
    s_vocab_phonetic = NULL;
    s_vocab_meaning = NULL;
    s_vocab_progress = NULL;
    s_vocab_feedback = NULL;
    s_dict_prompt = NULL;
    s_dict_ta = NULL;
    s_dict_kb = NULL;
    s_dict_feedback = NULL;
    s_dict_check = NULL;
    s_hand_canvas = NULL;
    s_hand_mode_btn = NULL;
    s_hand_hint = NULL;
    s_hand_add = NULL;
    s_hand_clear = NULL;
    s_hand_del = NULL;
    s_flappy_board = NULL;
    s_flappy_bird = NULL;
    s_flappy_score = NULL;
    s_flappy_status = NULL;
    for (int i = 0; i < FLAPPY_PIPE_COUNT; ++i) {
        s_flappy_pipe_top[i] = NULL;
        s_flappy_pipe_bottom[i] = NULL;
    }
    s_plan_value = NULL;
    s_plan_stats = NULL;
    s_plan_edit_target = 0;
    s_plan_notice[0] = '\0';
    s_vocab_reset_armed = false;
    s_pcmon_clock = NULL;
    s_pcmon_date = NULL;
    s_pcmon_link = NULL;
    s_pcmon_cpu_bar = NULL;
    s_pcmon_cpu_val = NULL;
    s_pcmon_gpu_bar = NULL;
    s_pcmon_gpu_val = NULL;
    s_pcmon_mem_bar = NULL;
    s_pcmon_mem_val = NULL;
    s_pcmon_mem_detail = NULL;
    s_pcmon_status = NULL;

    do_draw_all();
}

static void do_start_timer(void)
{
    if (!s_ui_timer) {
        s_ui_timer = lv_timer_create(ui_timer_cb, 500, NULL);
    }
}

void ui_process_cmd(int type, int arg)
{
    switch (type) {
        case UI_CMD_SCREEN:  do_set_screen((ui_screen_t)arg); break;
        case UI_CMD_DRAW:    do_draw_all(); break;
        case UI_CMD_STATUS:  do_update_status(); break;
        case UI_CMD_START_TIMER: do_start_timer(); break;
        default: break;
    }
}

void ui_init(void)
{
    s_auto = false;
    s_scanning = false;
    s_last_scan_us = esp_timer_get_time();
    s_screen = SCREEN_MENU;
    s_last_wifi_state = (wifi_state_t)-1;
    s_last_click_ms = 0;
    s_screen_enter_ms = 0;
    lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_MENU);
}

ui_screen_t ui_get_screen(void) { return s_screen; }

void ui_set_screen(ui_screen_t scr) { lv_port_post_cmd(UI_CMD_SCREEN, (int)scr); }
void ui_draw_all(void) { lv_port_post_cmd(UI_CMD_DRAW, 0); }
void ui_update_status(void) { lv_port_post_cmd(UI_CMD_STATUS, 0); }

void ui_set_scanning(bool scanning) { s_scanning = scanning; }
bool ui_get_scanning(void) { return s_scanning; }
void ui_set_auto(bool enabled) { s_auto = enabled; }
bool ui_get_auto(void) { return s_auto; }

void ui_wifi_state_changed(void)
{
    s_last_wifi_state = (wifi_state_t)-1;
    if (s_screen == SCREEN_WIFI) lv_port_post_cmd(UI_CMD_STATUS, 0);
}

const char *ui_wifi_get_pass(void)
{
    return s_wifi_ta ? lv_textarea_get_text(s_wifi_ta) : "";
}

int ui_wifi_get_selected(void) { return s_wifi_sel; }
void ui_wifi_select_ap(int idx) { s_wifi_sel = idx; }

static void poll_vocab_open_status(void)
{
    const vocab_open_status_t *op = vocab_open_status();
    if (!op) return;
    if (op->state == VOCAB_OPEN_READY) {
        vocab_mode_t mode = op->mode;
        vocab_open_ack();
        s_vocab_home_notice[0] = '\0';
        lv_port_post_cmd(UI_CMD_SCREEN, mode == VOCAB_MODE_DICTATION ? SCREEN_VOCAB_DICTATION : SCREEN_VOCAB_LEARN);
        return;
    }
    if (op->state == VOCAB_OPEN_ERROR) {
        char msg[64]; snprintf(msg, sizeof(msg), "%s", op->message); vocab_open_ack();
        if (s_screen == SCREEN_VOCAB_BOOKS) snprintf(s_vocab_books_notice, sizeof(s_vocab_books_notice), "OPEN FAILED\n%s", msg);
        else { snprintf(s_vocab_home_notice, sizeof(s_vocab_home_notice), "%s", msg); if (s_screen == SCREEN_VOCAB) refresh_vocab_dashboard(); }
    }
}

static void ui_timer_cb(lv_timer_t *t)
{
    (void)t;
    poll_vocab_open_status();
    switch (s_screen) {
        case SCREEN_SD:
            refresh_sd_labels();
            break;
        case SCREEN_SYSINFO:
            refresh_sysinfo_labels();
            break;
        case SCREEN_WIFI:
            if (wifi_mgr_get_state() != s_last_wifi_state) {
                s_last_wifi_state = wifi_mgr_get_state();
                refresh_wifi_labels();
                // On a successful connect while we were on the password view,
                // jump back to the AP list so the user can pick another WiFi
                // (or disconnect) without extra steps.
                if (s_last_wifi_state == WIFI_STATE_CONNECTED && s_wifi_sel >= 0) {
                    s_wifi_sel = -1;
                    lv_port_post_cmd(UI_CMD_DRAW, 0);
                }
            }
            break;
        case SCREEN_MENU:
            refresh_menu_bottom();
            break;
        case SCREEN_PCMON:
            refresh_pcmon_screen();
            break;
        case SCREEN_VOCAB:
            refresh_vocab_dashboard();
            break;
        case SCREEN_VOCAB_BOOKS: {
            const vocab_download_status_t *dl = vocab_download_status();
            vocab_download_state_t now = dl ? dl->state : VOCAB_DL_IDLE;
            refresh_vocab_download_label();
            if (now != s_last_dl_state) {
                bool just_installed = (now == VOCAB_DL_OK);
                s_last_dl_state = now;
                if (just_installed) lv_port_post_cmd(UI_CMD_DRAW, 0);
            }
            break;
        }
        case SCREEN_VOCAB_PLAN:
            if (s_plan_edit_target == 0) s_plan_edit_target = vocab_get_daily_target();
            refresh_plan_labels();
            break;
        default:
            break;
    }
}

void ui_tick(void)
{
    // Called from main loop; LVGL refresh is driven by ui_timer_cb instead.
}

void ui_start_timer(void)
{
    lv_port_post_cmd(UI_CMD_START_TIMER, 0);
}
