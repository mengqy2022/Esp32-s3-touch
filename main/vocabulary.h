#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOCAB_BOOK_DIR      "/sdcard/vocabulary/books"
#define VOCAB_PROGRESS_DIR  "/sdcard/vocabulary/progress"
#define VOCAB_CACHE_DIR     "/sdcard/vocabulary/cache"

#define VOCAB_FILENAME_MAX  48
#define VOCAB_TITLE_MAX     48
#define VOCAB_WORD_MAX      48
#define VOCAB_PHONETIC_MAX  72
#define VOCAB_MEANING_MAX   256
#define VOCAB_EXAMPLE_MAX   192
#define VOCAB_LOCAL_MAX     24

typedef enum {
    VOCAB_MODE_LEARN = 0,
    VOCAB_MODE_DICTATION,
} vocab_mode_t;

typedef struct {
    char filename[VOCAB_FILENAME_MAX];
    char title[VOCAB_TITLE_MAX];
    uint32_t word_count;
    bool selected;
} vocab_book_info_t;

typedef struct {
    char word[VOCAB_WORD_MAX];
    char phonetic[VOCAB_PHONETIC_MAX];
    char meaning[VOCAB_MEANING_MAX];
    char example[VOCAB_EXAMPLE_MAX];
    uint32_t word_index;
} vocab_word_t;

typedef struct {
    uint32_t total_words;
    uint32_t learned_words;
    uint32_t new_words;
    uint32_t correct_total;
    uint32_t wrong_total;
    uint32_t due_words;
    uint16_t today_done;
    uint16_t daily_target;
} vocab_stats_t;

typedef struct {
    const char *id;
    const char *title;
    const char *category;
    const char *url;
    const char *filename;
} vocab_catalog_item_t;

typedef enum {
    VOCAB_DL_IDLE = 0,
    VOCAB_DL_DOWNLOADING,
    VOCAB_DL_CONVERTING,
    VOCAB_DL_OK,
    VOCAB_DL_ERROR,
} vocab_download_state_t;

typedef struct {
    vocab_download_state_t state;
    size_t bytes_done;
    size_t bytes_total;
    int catalog_index;
    char message[64];
} vocab_download_status_t;

// Background book preparation used by WORDBOOKS. JSON import, CSV validation,
// progress-file preparation and the first session are all kept off the LVGL
// thread so the touch UI remains responsive.
typedef enum {
    VOCAB_OPEN_IDLE = 0,
    VOCAB_OPEN_PREPARING,
    VOCAB_OPEN_IMPORTING,
    VOCAB_OPEN_INDEXING,
    VOCAB_OPEN_PLANNING,
    VOCAB_OPEN_READY,
    VOCAB_OPEN_ERROR,
} vocab_open_state_t;

typedef struct {
    vocab_open_state_t state;
    vocab_mode_t mode;
    size_t bytes_done;
    size_t bytes_total;
    uint32_t words_done;
    char filename[VOCAB_FILENAME_MAX];
    char message[64];
} vocab_open_status_t;

// Initialize settings/NVS state. Safe before SD is mounted.
esp_err_t vocab_init(void);

// Create the SD directory structure. Requires an already-mounted SD card.
bool vocab_ensure_dirs(void);

// Local books (.csv) stored under VOCAB_BOOK_DIR.
int vocab_list_books(vocab_book_info_t *out, int max_items);
esp_err_t vocab_select_book(const char *filename);
bool vocab_has_active_book(void);
const char *vocab_active_book(void);
uint32_t vocab_active_word_count(void);

// Daily plan (5..100 words/day). Stored in NVS.
uint16_t vocab_get_daily_target(void);
esp_err_t vocab_set_daily_target(uint16_t target);

// Progress/statistics for the active book.
bool vocab_get_stats(vocab_stats_t *out);
esp_err_t vocab_reset_active_progress(void);

// Learning/dictation session.
esp_err_t vocab_start_session(vocab_mode_t mode);
int vocab_session_count(void);
int vocab_session_position(void); // zero-based
bool vocab_session_finished(void);
bool vocab_get_current(vocab_word_t *out);
bool vocab_mark_current(bool correct); // persists SRS progress once per word
bool vocab_next(void);
bool vocab_check_dictation(const char *answer);

// Non-blocking local/installed book open. On READY the requested session has
// already been built and the UI can switch screens immediately.
esp_err_t vocab_open_book_async(const char *filename, vocab_mode_t mode);
const vocab_open_status_t *vocab_open_status(void);
void vocab_open_ack(void);

// True after at least one trustworthy date has been obtained. If the current
// boot has no network time yet, the last trusted date is retained so progress
// never rolls over to 1970 by mistake.
bool vocab_daily_date_known(void);

// Built-in remote catalog (MIT-licensed ECDICT-derived data).
int vocab_catalog_count(void);
bool vocab_catalog_get(int index, vocab_catalog_item_t *out);
bool vocab_catalog_is_downloaded(int index);
esp_err_t vocab_download_catalog_async(int index);
const vocab_download_status_t *vocab_download_status(void);

#ifdef __cplusplus
}
#endif
