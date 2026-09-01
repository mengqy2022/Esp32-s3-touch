#include "ui.h"
#include "inventory.h"
#include "lcd_ili9341.h"
#include "lv_port.h"
#include "net_utils.h"
#include "sd_monitor.h"
#include "sudoku.h"
#include "game2048.h"
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

// SUDOKU state
static sudoku_t s_sudoku;
static game2048_t s_game2048;
static lv_obj_t *s_2048_cells[4][4];
static lv_obj_t *s_2048_score;
static lv_obj_t *s_2048_status;
static lv_point_t s_2048_touch_start;
static bool s_2048_touch_active;
static int s_sudoku_puzzle;
static lv_obj_t *s_sudo_cells[9][9];
static lv_obj_t *s_sudo_msg;

// INVENTORY state
#define INV_ROW_POOL  8
#define INV_ROW_H     21

typedef struct {
    lv_obj_t *row;
    lv_obj_t *spec;
    lv_obj_t *qty;
    lv_obj_t *lcsc;
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
static char s_inv_query[40];    // search filter for SPEC/LCSC
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

#define HAND_W 280
#define HAND_H 100
#define HAND_MAX_POINTS 256
#define HAND_BREAK_COORD (-32768)
static lv_obj_t *s_hand_canvas;
static lv_obj_t *s_hand_mode_btn;
static lv_obj_t *s_hand_hint;
static lv_obj_t *s_hand_preview;
static lv_obj_t *s_hand_add;
static lv_obj_t *s_hand_clear;
static lv_obj_t *s_hand_del;
/* Two-color indexed canvas: keeps handwriting visible while saving ~33 KB DRAM. */
static uint8_t s_hand_canvas_buf[
    LV_CANVAS_BUF_SIZE_INDEXED_1BIT(HAND_W, HAND_H)
];
static lv_point_t s_hand_points[HAND_MAX_POINTS];
static int s_hand_point_count;
static bool s_hand_pen_down;
static lv_point_t s_hand_prev;
static bool s_dict_hand_mode = true;
static lv_obj_t *s_plan_value;
static lv_obj_t *s_plan_stats;
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
            // Re-run touch calibration: suspend LVGL first so the bare-metal
            // LCD driver owns the SPI bus exclusively.
            lv_port_suspend();
            touch_clear_calibration();
            touch_run_calibration();
            lv_port_resume();
            lv_port_post_cmd(UI_CMD_SCREEN, SCREEN_MENU);
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
        { "CALIBRATE",  3, lv_color_hex(0xF5A623) },
        { "SUDOKU",     4, lv_color_hex(0xE0556D) },
        { "2048",       7, lv_color_hex(0xF97316) },
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
        label_set_text_if_changed(s_sd_banner, sd_state_text(s->state));
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

    snprintf(line, sizeof(line), "BYTES: %u", (unsigned)s->last_read_bytes);
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
// 3-column table (SPEC | QTY | LCSC) with filters, search, qty editing and
// stock history. Stock file is managed by the PC tool (INVENTORY.CSV).

#define INV_TBL_BG    lv_color_hex(0xF4F7FB)  // light table background
#define INV_TBL_BLACK lv_color_hex(0x11151C)  // black text on light bg

static bool inv_item_visible(const inv_item_t *it)
{
    if (!it) return false;
    if (s_inv_filter == 1 && it->qty <= 0) return false;
    if (s_inv_filter == 2 && it->qty > 0) return false;
    if (s_inv_query[0] &&
        strcasestr(it->spec, s_inv_query) == NULL &&
        strcasestr(it->lcsc, s_inv_query) == NULL &&
        strcasestr(it->name, s_inv_query) == NULL) {
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
    lv_label_set_text(lbl, "Filter NAME / SPEC / LCSC");
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
                        s_inv.items[s_inv_edit_idx].spec,
                        s_inv.items[s_inv_edit_idx].lcsc, 1);
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
                    s_inv.items[s_inv_edit_idx].spec,
                    s_inv.items[s_inv_edit_idx].lcsc, 1);
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
    bool hist_ok = inv_hist_append(stamp, "DEL", it->spec, it->lcsc, it->qty);
    ESP_LOGI(TAG, "inv: DEL idx=%d spec=%s lcsc=%s qty=%ld hist=%d",
             s_inv_edit_idx, it->spec, it->lcsc, (long)it->qty, (int)hist_ok);

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

        r->spec = lv_label_create(r->row);
        lv_label_set_long_mode(r->spec, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(r->spec, INV_TBL_BLACK, 0);
        lv_obj_set_style_text_align(r->spec, LV_TEXT_ALIGN_LEFT, 0);

        r->qty = lv_label_create(r->row);
        lv_obj_set_style_text_color(r->qty, INV_TBL_BLACK, 0);
        lv_obj_set_style_text_align(r->qty, LV_TEXT_ALIGN_CENTER, 0);

        r->lcsc = lv_label_create(r->row);
        lv_label_set_long_mode(r->lcsc, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(r->lcsc, INV_TBL_BLACK, 0);
        lv_obj_set_style_text_align(r->lcsc, LV_TEXT_ALIGN_LEFT, 0);

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

    lv_obj_t *h1 = make_btn(scr, "SPEC", on_inv_search, 0);
    lv_obj_set_size(h1, 70, 18);
    lv_obj_set_pos(h1, 10, 82);
    lv_obj_set_style_bg_color(h1, CLR_PANEL, 0);
    lv_obj_set_style_text_color(h1, CLR_ACCENT, 0);
    lv_obj_t *h2 = lv_label_create(scr);
    lv_label_set_text(h2, "QTY");
    lv_obj_set_style_text_color(h2, CLR_ACCENT, 0);
    lv_obj_set_pos(h2, 172, 85);
    lv_obj_t *h3 = make_btn(scr, "LCSC", on_inv_search, 0);
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
            char line[96];

            if (is_del) {
                snprintf(line, sizeof(line), "%s %s %s x%ld", LV_SYMBOL_TRASH,
                         h->stamp, h->spec[0] ? h->spec : h->lcsc, (long)h->qty);
            } else {
                snprintf(line, sizeof(line), "%s %s %s x%ld", act,
                         h->stamp, h->spec[0] ? h->spec : h->lcsc, (long)h->qty);
            }

            label_set_text_if_changed(r->spec, line);
            lv_obj_set_pos(r->spec, 6, 2);
            lv_obj_set_size(r->spec, 278, 16);
            lv_obj_add_flag(r->qty, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->lcsc, LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_clear_flag(r->lcsc, LV_OBJ_FLAG_HIDDEN);

        int c1w = selected ? 100 : 168;
        int c2x = selected ? 110 : 178;
        int c2w = selected ? 38 : 42;
        int c3x = selected ? 150 : 224;
        int c3w = selected ? 68 : 62;

        label_set_text_if_changed(r->spec, it->spec[0] ? it->spec :
                                          (it->name[0] ? it->name : it->lcsc));
        lv_obj_set_pos(r->spec, 6, 3);
        lv_obj_set_size(r->spec, c1w, 16);

        char qbuf[16];
        snprintf(qbuf, sizeof(qbuf), "x%ld", (long)it->qty);
        label_set_text_if_changed(r->qty, qbuf);
        lv_obj_set_pos(r->qty, c2x, 3);
        lv_obj_set_size(r->qty, c2w, 16);

        label_set_text_if_changed(r->lcsc, it->lcsc);
        lv_obj_set_pos(r->lcsc, c3x, 3);
        lv_obj_set_size(r->lcsc, c3w, 16);

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

    s_dict_prompt = make_label(scr, "Write the word here", 10, 42, CLR_TEXT);
    s_dict_feedback = make_label(scr, "Handwriting pad enabled", 10, 220, CLR_TEXT_DIM);

    s_hand_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_hand_canvas, s_hand_canvas_buf, HAND_W, HAND_H, LV_IMG_CF_INDEXED_1BIT);
    lv_canvas_fill_bg(s_hand_canvas, lv_color_white(), LV_OPA_COVER);
    lv_obj_set_pos(s_hand_canvas, 20, 75);
    lv_obj_set_size(s_hand_canvas, HAND_W, HAND_H);
    lv_obj_add_event_cb(s_hand_canvas, hand_draw_event, LV_EVENT_ALL, NULL);

    s_hand_clear = make_btn(scr, "CLEAR", hand_clear_event, 0);
    lv_obj_set_size(s_hand_clear, 90, 35);
    lv_obj_set_pos(s_hand_clear, 110, 180);

    s_dict_check = make_btn(scr, "CHECK", NULL, 0);
    lv_obj_set_size(s_dict_check, 90, 35);
    lv_obj_set_pos(s_dict_check, 210, 180);

    hand_clear_pad();
}

static void build_vocab_plan_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    style_screen(scr);
    make_title(scr, "Study Plan");
    make_back_btn(scr);

    s_plan_value = make_label(scr, "Plan ready", 20, 80, CLR_TEXT);
    s_plan_stats = make_label(scr, "", 20, 120, CLR_TEXT_DIM);
}

static void refresh_vocab_dictation(void)
{
    if (s_dict_feedback) {
        lv_label_set_text(s_dict_feedback, "Type the word and check");
    }
}

static void hand_clear_pad(void)
{
    s_hand_point_count = 0;
    s_hand_pen_down = false;
    if (s_hand_canvas) {
        lv_canvas_fill_bg(s_hand_canvas, lv_color_white(), LV_OPA_COVER);
        lv_obj_invalidate(s_hand_canvas);
    }
}

static void hand_clear_event(lv_event_t *e)
{
    (void)e;
    hand_clear_pad();
}

static void hand_draw_event(lv_event_t *e)
{
    if (!s_hand_canvas) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSING && code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    p.x -= lv_obj_get_x(s_hand_canvas);
    p.y -= lv_obj_get_y(s_hand_canvas);

    if (p.x < 0 || p.y < 0 || p.x >= HAND_W || p.y >= HAND_H) return;

    if (code == LV_EVENT_RELEASED) {
        s_hand_pen_down = false;
        return;
    }

    if (s_hand_pen_down) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = lv_color_black();
        dsc.width = 3;
        lv_point_t pts[2] = {s_hand_prev, p};
        lv_canvas_draw_line(s_hand_canvas, pts, 2, &dsc);
    }

    s_hand_prev = p;
    s_hand_pen_down = true;
    if (s_hand_point_count < HAND_MAX_POINTS) {
        s_hand_points[s_hand_point_count++] = p;
    }
    lv_obj_invalidate(s_hand_canvas);
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

static void refresh_plan_labels(void)
{
    if (s_plan_value) {
        lv_label_set_text(s_plan_value, "Plan ready");
    }
    if (s_plan_stats) {
        lv_label_set_text(s_plan_stats, "");
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
        case SCREEN_VOCAB:   refresh_vocab_dashboard(); break;
        case SCREEN_VOCAB_BOOKS: refresh_vocab_download_label(); break;
        case SCREEN_VOCAB_PLAN: refresh_plan_labels(); break;
        default: break;
    }
}

static void do_set_screen(ui_screen_t scr)
{
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
    s_plan_value = NULL;
    s_plan_stats = NULL;
    s_vocab_reset_armed = false;

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
