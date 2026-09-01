#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// INVENTORY.CSV is intentionally minimal and UTF-8 encoded:
// PRODUCT_NO,MODEL,QTY
#define INV_PRODUCT_NO_MAX  24
#define INV_MODEL_MAX       64
#define INV_MAX_ITEMS       256

typedef struct {
    char product_no[INV_PRODUCT_NO_MAX]; // supplier/LCSC product number, e.g. C123456
    char model[INV_MODEL_MAX];           // manufacturer/product model
    int32_t qty;                         // quantity on hand
} inv_item_t;

typedef struct {
    inv_item_t items[INV_MAX_ITEMS];
    int count;
} inv_db_t;

typedef struct {
    char product_no[INV_PRODUCT_NO_MAX];
    char model[INV_MODEL_MAX];
    int32_t qty;
} inv_bom_line_t;

#define INV_BOM_MAX_LINES 50

typedef struct {
    int bom_idx;
    int matched;        // -1 = not found, >=0 = inventory index
    bool is_new;        // reserved
    int32_t qty;
} inv_match_t;

#define INV_FILE        "/sdcard/INVENTORY.CSV"
#define INV_BOM_IN_DIR  "/sdcard/BOM_IN"
#define INV_BOM_OUT_DIR "/sdcard/BOM_OUT"
#define INV_HIST_FILE   "/sdcard/STOCK_HIST.CSV"

#define INV_HIST_MAX 50
typedef struct {
    char stamp[24];
    char action[8];
    char model[INV_MODEL_MAX];
    char product_no[INV_PRODUCT_NO_MAX];
    int32_t qty;
} inv_hist_t;

bool inv_hist_append(const char *stamp, const char *action, const char *model,
                     const char *product_no, int32_t qty);
int inv_hist_load(inv_hist_t *hist, int max);

int inv_load(inv_db_t *db);
bool inv_save(const inv_db_t *db);
bool inv_ensure_folders(void);

// Parse UTF-8 CSV. Header may be preceded by summary lines. Supported key columns:
// 商品编号 / PRODUCT_NO / LCSC, 商品型号 / MODEL / SPEC, 订购数量 / QTY.
int inv_parse_bom(const char *path, inv_bom_line_t *lines, int max_lines);
int inv_match_bom(const inv_db_t *db, const inv_bom_line_t *lines, int line_count,
                  inv_match_t *matches);
int inv_apply_bom(inv_db_t *db, const inv_bom_line_t *lines, int line_count,
                  const inv_match_t *matches, bool add);
int inv_find_item(const inv_db_t *db, const inv_bom_line_t *line);

#ifdef __cplusplus
}
#endif
