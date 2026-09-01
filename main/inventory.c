#include "inventory.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "inv";

// ---------------- CSV helpers ----------------

// Parse one CSV line into up to 5 fields. Handles quoted fields and commas
// inside quotes. Returns number of fields parsed (may be < max).
static int csv_split(char *line, char *fields[], int max_fields)
{
    if (!line || !fields || max_fields <= 0) return 0;

    // Single-pass in-place CSV split. This avoids the old memmove-per-quote
    // behavior (quadratic on long quoted lines) and also accepts escaped "".
    int n = 0;
    bool in_quotes = false;
    char *r = line;
    char *w = line;
    char *start = w;

    while (*r) {
        if (*r == '"') {
            if (in_quotes && r[1] == '"') {
                *w++ = '"';
                r += 2;
                continue;
            }
            in_quotes = !in_quotes;
            ++r;
            continue;
        }

        if (*r == ',' && !in_quotes && n < max_fields - 1) {
            *w++ = '\0';
            fields[n++] = start;
            start = w;
            ++r;
            continue;
        }

        *w++ = *r++;
    }

    *w = '\0';
    if (n < max_fields) fields[n++] = start;
    return n;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static int str_icmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        ++a; ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

// Does the header cell match any of the given keywords?
static bool header_matches(const char *cell, const char *const *keywords, int n)
{
    char c[64];
    snprintf(c, sizeof(c), "%s", cell);
    char *t = trim(c);
    for (int i = 0; i < n; ++i) {
        if (str_icmp(t, keywords[i]) == 0) return true;
        // Also match substring like "LCSC Part #"
        if (strstr(t, keywords[i])) return true;
    }
    return false;
}

typedef struct {
    int col_qty, col_lcsc, col_name, col_spec, col_pkg;
    bool valid;
} bom_cols_t;

static const char *KW_QTY[]   = { "qty", "quantity", "count", "数量" };
static const char *KW_LCSC[]  = { "lcsc", "lcsc part#", "lcsc编号", "编号", "part#" };
static const char *KW_NAME[]  = { "name", "value", "part", "型号", "名称", "comment" };
static const char *KW_SPEC[]  = { "spec", "specification", "description", "规格", "描述" };
static const char *KW_PKG[]   = { "package", "footprint", "封装" };

static bom_cols_t detect_columns(char *header_line)
{
    bom_cols_t c = { -1, -1, -1, -1, -1, false };
    char *fields[16];
    int n = csv_split(header_line, fields, 16);
    for (int i = 0; i < n; ++i) {
        char *f = trim(fields[i]);
        if (header_matches(f, KW_QTY, 4)) c.col_qty = i;
        if (header_matches(f, KW_LCSC, 5)) c.col_lcsc = i;
        if (header_matches(f, KW_NAME, 6)) c.col_name = i;
        if (header_matches(f, KW_SPEC, 5)) c.col_spec = i;
        if (header_matches(f, KW_PKG, 3)) c.col_pkg = i;
    }
    // Need at least a qty column (or name) to be usable.
    if (c.col_qty >= 0 && (c.col_name >= 0 || c.col_lcsc >= 0)) c.valid = true;
    return c;
}

// ---------------- Inventory file ----------------

bool inv_ensure_folders(void)
{
    bool ok = true;
    DIR *d = opendir(INV_BOM_IN_DIR);
    if (d) {
        closedir(d);
    } else if (mkdir(INV_BOM_IN_DIR, 0777) != 0) {
        ok = false;
    }
    d = opendir(INV_BOM_OUT_DIR);
    if (d) {
        closedir(d);
    } else if (mkdir(INV_BOM_OUT_DIR, 0777) != 0) {
        ok = false;
    }
    return ok;
}

int inv_load(inv_db_t *db)
{
    if (!db) return 0;
    memset(db, 0, sizeof(*db));

    FILE *f = fopen(INV_FILE, "r");
    if (!f) return 0;

    // FATFS benefits from a larger stdio buffer when loading many rows.
    char io_buf[1024];
    (void)setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));

    char line[512];
    bool first = true;
    while (db->count < INV_MAX_ITEMS && fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '\0') continue;

        char *fields[8];
        int n = csv_split(line, fields, 8);
        if (first) { first = false; continue; } // header
        if (n < 2) continue;

        inv_item_t *it = &db->items[db->count];
        memset(it, 0, sizeof(*it));
        snprintf(it->lcsc, sizeof(it->lcsc), "%s", trim(fields[0]));
        snprintf(it->name, sizeof(it->name), "%s", trim(fields[1]));
        if (n > 2) snprintf(it->spec, sizeof(it->spec), "%s", trim(fields[2]));
        if (n > 3) snprintf(it->pkg,  sizeof(it->pkg),  "%s", trim(fields[3]));
        if (n > 4) it->qty = (int32_t)strtol(fields[4], NULL, 10);
        db->count++;
    }
    fclose(f);
    ESP_LOGI(TAG, "loaded %d items from %s", db->count, INV_FILE);
    return db->count;
}

bool inv_save(const inv_db_t *db)
{
    if (!db) return false;
    FILE *f = fopen(INV_FILE, "w");
    if (!f) return false;

    // Reduce small FATFS writes; this materially shortens +/- quantity edits
    // when INVENTORY.CSV contains hundreds of rows.
    char io_buf[1024];
    (void)setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));

    fprintf(f, "LCSC,NAME,SPEC,PACKAGE,QTY\n");
    for (int i = 0; i < db->count; ++i) {
        const inv_item_t *it = &db->items[i];
        fprintf(f, "%s,%s,%s,%s,%ld\n",
                it->lcsc, it->name, it->spec, it->pkg, (long)it->qty);
    }
    fclose(f);
    ESP_LOGI(TAG, "saved %d items to %s", db->count, INV_FILE);
    return true;
}

// ---------------- BOM parsing ----------------

int inv_parse_bom(const char *path, inv_bom_line_t *lines, int max_lines)
{
    if (!path || !lines || max_lines <= 0) return 0;

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "cannot open BOM: %s", path);
        return 0;
    }

    char line[512];
    bool first = true;
    bom_cols_t cols = { -1, -1, -1, -1, -1, false };
    int count = 0;

    while (count < max_lines && fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '\0') continue;

        if (first) {
            cols = detect_columns(line);
            first = false;
            if (!cols.valid) {
                ESP_LOGW(TAG, "BOM header not recognized: %s", path);
                fclose(f);
                return 0;
            }
            continue;
        }

        char *fields[16];
        int n = csv_split(line, fields, 16);
        if (n <= cols.col_qty && n <= cols.col_name && n <= cols.col_lcsc) continue;

        inv_bom_line_t *b = &lines[count];
        memset(b, 0, sizeof(*b));

        const char *q = (cols.col_qty >= 0 && cols.col_qty < n) ? fields[cols.col_qty] : "1";
        b->qty = (int32_t)strtol(trim((char *)q), NULL, 10);
        if (b->qty < 1) b->qty = 1;

        if (cols.col_lcsc >= 0 && cols.col_lcsc < n)
            snprintf(b->lcsc, sizeof(b->lcsc), "%s", trim(fields[cols.col_lcsc]));
        if (cols.col_name >= 0 && cols.col_name < n)
            snprintf(b->name, sizeof(b->name), "%s", trim(fields[cols.col_name]));
        if (cols.col_spec >= 0 && cols.col_spec < n)
            snprintf(b->spec, sizeof(b->spec), "%s", trim(fields[cols.col_spec]));
        if (cols.col_pkg >= 0 && cols.col_pkg < n)
            snprintf(b->pkg, sizeof(b->pkg), "%s", trim(fields[cols.col_pkg]));

        // Skip pure header-like rows.
        if (b->name[0] == '\0' && b->lcsc[0] == '\0') continue;
        // Skip "Designator" style rows that only have designators.
        if (str_icmp(b->name, "Designator") == 0) continue;

        count++;
    }
    fclose(f);
    ESP_LOGI(TAG, "parsed %d lines from %s", count, path);
    return count;
}

int inv_find_item(const inv_db_t *db, const inv_bom_line_t *line)
{
    if (!db || !line) return -1;

    // 1) LCSC exact match (case-insensitive)
    if (line->lcsc[0]) {
        for (int i = 0; i < db->count; ++i) {
            if (db->items[i].lcsc[0] && str_icmp(db->items[i].lcsc, line->lcsc) == 0) {
                return i;
            }
        }
    }
    // 2) name + spec + package match (name required)
    if (line->name[0]) {
        for (int i = 0; i < db->count; ++i) {
            if (str_icmp(db->items[i].name, line->name) != 0) continue;
            bool spec_ok = !line->spec[0] || !db->items[i].spec[0] ||
                           str_icmp(db->items[i].spec, line->spec) == 0;
            bool pkg_ok  = !line->pkg[0]  || !db->items[i].pkg[0]  ||
                           str_icmp(db->items[i].pkg, line->pkg) == 0;
            if (spec_ok && pkg_ok) return i;
        }
    }
    return -1;
}

int inv_match_bom(const inv_db_t *db, const inv_bom_line_t *lines, int line_count,
                  inv_match_t *matches)
{
    if (!db || !lines || !matches) return 0;
    int found = 0;
    for (int i = 0; i < line_count; ++i) {
        matches[i].bom_idx = i;
        matches[i].matched = inv_find_item(db, &lines[i]);
        matches[i].qty = lines[i].qty;
        matches[i].is_new = false;
        if (matches[i].matched >= 0) found++;
    }
    return found;
}

int inv_apply_bom(inv_db_t *db, const inv_bom_line_t *lines, int line_count,
                  const inv_match_t *matches, bool add)
{
    if (!db || !lines || !matches) return 0;
    int applied = 0;
    for (int i = 0; i < line_count; ++i) {
        int idx = matches[i].matched;
        if (idx < 0 || idx >= db->count) continue;
        if (add) {
            db->items[idx].qty += matches[i].qty;
        } else {
            db->items[idx].qty -= matches[i].qty;
            if (db->items[idx].qty < 0) db->items[idx].qty = 0;
        }
        applied++;
    }
    return applied;
}

// ---------------- Stock history (STOCK_HIST.CSV) ----------------

bool inv_hist_append(const char *stamp, const char *action, const char *spec,
                     const char *lcsc, int32_t qty)
{
    FILE *f = fopen(INV_HIST_FILE, "a");
    if (!f) {
        // File may not exist yet: create it with a header.
        ESP_LOGW(TAG, "hist: append open failed, trying create");
        f = fopen(INV_HIST_FILE, "w");
        if (!f) {
            ESP_LOGE(TAG, "hist: cannot create %s", INV_HIST_FILE);
            return false;
        }
        fprintf(f, "DATETIME,ACTION,SPEC,LCSC,QTY\n");
        fclose(f);
        f = fopen(INV_HIST_FILE, "a");
        if (!f) {
            ESP_LOGE(TAG, "hist: reopen failed");
            return false;
        }
    }
    int rc = fprintf(f, "%s,%s,%s,%s,%ld\n",
            stamp ? stamp : "-", action ? action : "-",
            spec ? spec : "-", lcsc ? lcsc : "-", (long)qty);
    fclose(f);
    if (rc < 0) {
        ESP_LOGE(TAG, "hist: fprintf failed");
        return false;
    }
    ESP_LOGI(TAG, "hist: appended %s %s %s x%ld", action, spec, lcsc, (long)qty);
    return true;
}

int inv_hist_load(inv_hist_t *hist, int max)
{
    if (!hist || max <= 0) return 0;
    memset(hist, 0, sizeof(inv_hist_t) * max);

    FILE *f = fopen(INV_HIST_FILE, "r");
    if (!f) return 0;

    char io_buf[1024];
    (void)setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));

    char line[256];
    bool first = true;
    int total = 0;

    // Circularly retain the newest `max` valid entries. This keeps history
    // useful even when STOCK_HIST.CSV grows well beyond the UI cache size.
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '\0') continue;
        if (first) { first = false; continue; } // header

        char *fields[8];
        int n = csv_split(line, fields, 8);
        if (n < 5) continue;

        int slot = total % max;
        inv_hist_t *h = &hist[slot];
        memset(h, 0, sizeof(*h));
        snprintf(h->stamp, sizeof(h->stamp), "%s", trim(fields[0]));
        snprintf(h->action, sizeof(h->action), "%s", trim(fields[1]));
        snprintf(h->spec, sizeof(h->spec), "%s", trim(fields[2]));
        snprintf(h->lcsc, sizeof(h->lcsc), "%s", trim(fields[3]));
        h->qty = (int32_t)strtol(fields[4], NULL, 10);
        total++;
    }
    fclose(f);

    int count = total < max ? total : max;
    if (total > max && count > 1) {
        // Ring order is [old tail ... newest ... old head]. Rotate in place
        // through a temporary cache; callers then receive oldest->newest.
        inv_hist_t *tmp = malloc(sizeof(inv_hist_t) * (size_t)count);
        if (tmp) {
            int oldest = total % max;
            for (int i = 0; i < count; ++i) {
                tmp[i] = hist[(oldest + i) % max];
            }
            memcpy(hist, tmp, sizeof(inv_hist_t) * (size_t)count);
            free(tmp);
        } else {
            ESP_LOGW(TAG, "history: no memory to reorder newest entries");
        }
    }

    ESP_LOGI(TAG, "history: %d newest entries (%d total)", count, total);
    return count;
}

