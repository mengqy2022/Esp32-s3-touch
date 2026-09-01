#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INV_LCSC_MAX    32
#define INV_NAME_MAX    32
#define INV_SPEC_MAX    40
#define INV_PKG_MAX     20
#define INV_MAX_ITEMS   256

typedef struct {
    char lcsc[INV_LCSC_MAX];   // LCSC part number, e.g. "C123456" (may be empty)
    char name[INV_NAME_MAX];   // component name / value
    char spec[INV_SPEC_MAX];   // specification / description
    char pkg[INV_PKG_MAX];     // package / footprint
    int32_t qty;               // quantity on hand
} inv_item_t;

typedef struct {
    inv_item_t items[INV_MAX_ITEMS];
    int count;
} inv_db_t;

// A single BOM line after parsing.
typedef struct {
    char lcsc[INV_LCSC_MAX];
    char name[INV_NAME_MAX];
    char spec[INV_SPEC_MAX];
    char pkg[INV_PKG_MAX];
    int32_t qty;
} inv_bom_line_t;

#define INV_BOM_MAX_LINES 50

// Result of matching one BOM line against the inventory.
typedef struct {
    int bom_idx;         // index in bom lines
    int matched;         // -1 = not found, >=0 = index in db
    bool is_new;         // matched by name+spec (not LCSC) => considered new? (reserved)
    int32_t qty;
} inv_match_t;

// Paths on the SD card
#define INV_FILE       "/sdcard/INVENTORY.CSV"
#define INV_BOM_IN_DIR "/sdcard/BOM_IN"
#define INV_BOM_OUT_DIR "/sdcard/BOM_OUT"
#define INV_HIST_FILE  "/sdcard/STOCK_HIST.CSV"

// One stock-history entry (append-only log: date,time,action,spec,lcsc,qty)
#define INV_HIST_MAX 50
typedef struct {
    char stamp[24];      // "YYYY-MM-DD HH:MM:SS"
    char action[8];      // "IN" / "OUT"
    char spec[INV_SPEC_MAX];
    char lcsc[INV_LCSC_MAX];
    int32_t qty;
} inv_hist_t;

// Append one history line to STOCK_HIST.CSV (creates header if missing).
bool inv_hist_append(const char *stamp, const char *action, const char *spec,
                     const char *lcsc, int32_t qty);
// Load history (most recent last). Returns entry count (0 if missing).
int inv_hist_load(inv_hist_t *hist, int max);

// Load inventory from SD card. Returns item count (0 on missing file).
int inv_load(inv_db_t *db);
// Save inventory to SD card. Returns true on success.
bool inv_save(const inv_db_t *db);
// Ensure the required SD folders (BOM_IN / BOM_OUT) exist. Returns true on success.
bool inv_ensure_folders(void);

// Parse a BOM CSV file into lines. Returns line count (0 on failure).
int inv_parse_bom(const char *path, inv_bom_line_t *lines, int max_lines);

// Match parsed BOM lines against the inventory (LCSC first, then name+spec).
// Fills matches[] (length = line_count). Returns number of lines matched to
// an existing item (found in db).
int inv_match_bom(const inv_db_t *db, const inv_bom_line_t *lines, int line_count,
                  inv_match_t *matches);

// Apply the matched BOM to the inventory: add (add=true) or subtract (add=false).
// Skips lines that were not found. Returns number of lines applied.
int inv_apply_bom(inv_db_t *db, const inv_bom_line_t *lines, int line_count,
                  const inv_match_t *matches, bool add);

// Find an item by LCSC (exact) or by name+spec+package. Returns index or -1.
int inv_find_item(const inv_db_t *db, const inv_bom_line_t *line);

#ifdef __cplusplus
}
#endif
