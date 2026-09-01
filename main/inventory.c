#include "inventory.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"

static const char *TAG = "inv";

// ---------------- CSV helpers ----------------

static int csv_split(char *line, char *fields[], int max_fields)
{
    if (!line || !fields || max_fields <= 0) return 0;
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
    if (!s) return s;
    // UTF-8 BOM on the first cell.
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF) {
        s += 3;
    }
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
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool contains_icase_ascii(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return false;
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == n) return true;
    }
    return false;
}

typedef struct { const char *key; int score; } header_rule_t;

static const header_rule_t RULE_QTY[] = {
    {"订购数量", 100}, {"quantity", 95}, {"qty", 95}, {"数量", 80}, {"count", 60},
};
static const header_rule_t RULE_PRODUCT[] = {
    {"商品编号", 100}, {"product_no", 100}, {"product no", 100}, {"lcsc", 95},
    {"supplier part", 90}, {"供应商编号", 90}, {"part#", 60}, {"编号", 40},
};
static const header_rule_t RULE_MODEL[] = {
    {"商品型号", 100}, {"manufacturer part", 98}, {"厂家型号", 98}, {"制造商型号", 98},
    {"model", 95}, {"specification", 85}, {"spec", 80}, {"规格", 75}, {"value", 65},
    {"商品名称", 45}, {"name", 40}, {"名称", 35},
};
static const header_rule_t RULE_NAME[] = {
    {"商品名称", 100}, {"name", 80}, {"名称", 70}, {"comment", 60},
};

static int cell_score(const char *cell, const header_rule_t *rules, size_t count)
{
    int best = -1;
    for (size_t i = 0; i < count; ++i) {
        const char *key = rules[i].key;
        bool ascii = true;
        for (const unsigned char *p = (const unsigned char *)key; *p; ++p) {
            if (*p >= 0x80) { ascii = false; break; }
        }
        bool match = ascii ? contains_icase_ascii(cell, key) : (strstr(cell, key) != NULL);
        if (match && rules[i].score > best) best = rules[i].score;
    }
    return best;
}

typedef struct {
    int col_qty;
    int col_product_no;
    int col_model;
    int col_name_fallback;
    bool valid;
} inv_cols_t;

static inv_cols_t detect_columns(char *header_line)
{
    inv_cols_t c = {-1, -1, -1, -1, false};
    int best_qty = -1, best_product = -1, best_model = -1, best_name = -1;
    char *fields[32];
    int n = csv_split(header_line, fields, 32);
    for (int i = 0; i < n; ++i) {
        char *f = trim(fields[i]);
        int s;
        s = cell_score(f, RULE_QTY, sizeof(RULE_QTY) / sizeof(RULE_QTY[0]));
        if (s > best_qty) { best_qty = s; c.col_qty = i; }
        s = cell_score(f, RULE_PRODUCT, sizeof(RULE_PRODUCT) / sizeof(RULE_PRODUCT[0]));
        if (s > best_product) { best_product = s; c.col_product_no = i; }
        s = cell_score(f, RULE_MODEL, sizeof(RULE_MODEL) / sizeof(RULE_MODEL[0]));
        if (s > best_model) { best_model = s; c.col_model = i; }
        s = cell_score(f, RULE_NAME, sizeof(RULE_NAME) / sizeof(RULE_NAME[0]));
        if (s > best_name) { best_name = s; c.col_name_fallback = i; }
    }
    c.valid = c.col_qty >= 0 && (c.col_product_no >= 0 || c.col_model >= 0 || c.col_name_fallback >= 0);
    return c;
}

static void strip_line_end(char *line)
{
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    char *cr = strchr(line, '\r');
    if (cr) *cr = '\0';
}

static void parse_inventory_row(char *line, const inv_cols_t *cols, inv_item_t *it)
{
    char *fields[32];
    int n = csv_split(line, fields, 32);
    memset(it, 0, sizeof(*it));
    if (cols->col_product_no >= 0 && cols->col_product_no < n) {
        snprintf(it->product_no, sizeof(it->product_no), "%s", trim(fields[cols->col_product_no]));
    }
    const char *model = "";
    if (cols->col_model >= 0 && cols->col_model < n) model = trim(fields[cols->col_model]);
    if ((!model || !*model) && cols->col_name_fallback >= 0 && cols->col_name_fallback < n) {
        model = trim(fields[cols->col_name_fallback]);
    }
    snprintf(it->model, sizeof(it->model), "%s", model ? model : "");
    if (cols->col_qty >= 0 && cols->col_qty < n) {
        it->qty = (int32_t)strtol(trim(fields[cols->col_qty]), NULL, 10);
    }
}

// ---------------- Inventory file ----------------

bool inv_ensure_folders(void)
{
    bool ok = true;
    DIR *d = opendir(INV_BOM_IN_DIR);
    if (d) closedir(d);
    else if (mkdir(INV_BOM_IN_DIR, 0777) != 0) ok = false;

    d = opendir(INV_BOM_OUT_DIR);
    if (d) closedir(d);
    else if (mkdir(INV_BOM_OUT_DIR, 0777) != 0) ok = false;
    return ok;
}

int inv_load(inv_db_t *db)
{
    if (!db) return 0;
    memset(db, 0, sizeof(*db));

    FILE *f = fopen(INV_FILE, "r");
    if (!f) return 0;
    char io_buf[1024];
    (void)setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));

    char line[768];
    inv_cols_t cols = {-1, -1, -1, -1, false};
    int header_scan = 0;
    while (fgets(line, sizeof(line), f)) {
        strip_line_end(line);
        if (!line[0]) continue;
        if (!cols.valid) {
            char header_copy[768];
            snprintf(header_copy, sizeof(header_copy), "%s", line);
            cols = detect_columns(header_copy);
            if (!cols.valid && ++header_scan < 10) continue;
            if (!cols.valid) break;
            continue;
        }
        if (db->count >= INV_MAX_ITEMS) break;
        inv_item_t item;
        parse_inventory_row(line, &cols, &item);
        if (!item.product_no[0] && !item.model[0]) continue;
        db->items[db->count++] = item;
    }
    fclose(f);
    ESP_LOGI(TAG, "loaded %d items from %s", db->count, INV_FILE);
    return db->count;
}

static void csv_write_clean(FILE *f, const char *s)
{
    // Current inventory source is expected to be model/part-number text. Replace
    // delimiters instead of creating a heavy quoted-field writer on the MCU.
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; ++p) {
        unsigned char c = *p;
        if (c == ',' || c == '"' || c == '\r' || c == '\n') fputc(' ', f);
        else fputc(c, f);
    }
}

bool inv_save(const inv_db_t *db)
{
    if (!db) return false;
    FILE *f = fopen(INV_FILE, "w");
    if (!f) return false;
    char io_buf[1024];
    (void)setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));

    fprintf(f, "PRODUCT_NO,MODEL,QTY\n");
    for (int i = 0; i < db->count; ++i) {
        const inv_item_t *it = &db->items[i];
        csv_write_clean(f, it->product_no);
        fputc(',', f);
        csv_write_clean(f, it->model);
        fprintf(f, ",%ld\n", (long)it->qty);
    }
    bool ok = (fclose(f) == 0);
    ESP_LOGI(TAG, "saved %d compact items to %s", db->count, INV_FILE);
    return ok;
}

// ---------------- BOM / purchase CSV parsing ----------------

int inv_parse_bom(const char *path, inv_bom_line_t *lines, int max_lines)
{
    if (!path || !lines || max_lines <= 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "cannot open BOM: %s", path);
        return 0;
    }

    char line[768];
    inv_cols_t cols = {-1, -1, -1, -1, false};
    int header_scan = 0;
    int count = 0;

    while (count < max_lines && fgets(line, sizeof(line), f)) {
        strip_line_end(line);
        if (!line[0]) continue;

        if (!cols.valid) {
            char header_copy[768];
            snprintf(header_copy, sizeof(header_copy), "%s", line);
            cols = detect_columns(header_copy);
            header_scan++;
            if (!cols.valid) {
                if (header_scan >= 24) break;
                continue;
            }
            ESP_LOGI(TAG, "CSV header found after %d non-empty lines", header_scan);
            continue;
        }

        inv_item_t item;
        parse_inventory_row(line, &cols, &item);
        if (!item.product_no[0] && !item.model[0]) continue;
        if (item.qty < 1) item.qty = 1;

        inv_bom_line_t *b = &lines[count++];
        memset(b, 0, sizeof(*b));
        snprintf(b->product_no, sizeof(b->product_no), "%s", item.product_no);
        snprintf(b->model, sizeof(b->model), "%s", item.model);
        b->qty = item.qty;
    }
    fclose(f);
    ESP_LOGI(TAG, "parsed %d compact lines from %s", count, path);
    return count;
}

int inv_find_item(const inv_db_t *db, const inv_bom_line_t *line)
{
    if (!db || !line) return -1;
    if (line->product_no[0]) {
        for (int i = 0; i < db->count; ++i) {
            if (db->items[i].product_no[0] && str_icmp(db->items[i].product_no, line->product_no) == 0) return i;
        }
    }
    // Only fall back to MODEL when the source has no product number. Different
    // supplier product numbers may legitimately share the same manufacturer model.
    if (!line->product_no[0] && line->model[0]) {
        for (int i = 0; i < db->count; ++i) {
            if (db->items[i].model[0] && str_icmp(db->items[i].model, line->model) == 0) return i;
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
        if (add) db->items[idx].qty += matches[i].qty;
        else {
            db->items[idx].qty -= matches[i].qty;
            if (db->items[idx].qty < 0) db->items[idx].qty = 0;
        }
        applied++;
    }
    return applied;
}

// ---------------- Stock history ----------------

bool inv_hist_append(const char *stamp, const char *action, const char *model,
                     const char *product_no, int32_t qty)
{
    FILE *f = fopen(INV_HIST_FILE, "a");
    if (!f) {
        f = fopen(INV_HIST_FILE, "w");
        if (!f) return false;
        fprintf(f, "DATETIME,ACTION,MODEL,PRODUCT_NO,QTY\n");
        fclose(f);
        f = fopen(INV_HIST_FILE, "a");
        if (!f) return false;
    }
    int rc = fprintf(f, "%s,%s,%s,%s,%ld\n",
                     stamp ? stamp : "-", action ? action : "-",
                     model ? model : "-", product_no ? product_no : "-", (long)qty);
    fclose(f);
    return rc >= 0;
}

int inv_hist_load(inv_hist_t *hist, int max)
{
    if (!hist || max <= 0) return 0;
    memset(hist, 0, sizeof(inv_hist_t) * (size_t)max);
    FILE *f = fopen(INV_HIST_FILE, "r");
    if (!f) return 0;

    char io_buf[768];
    (void)setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));
    char line[256];
    bool first = true;
    int total = 0;
    while (fgets(line, sizeof(line), f)) {
        strip_line_end(line);
        if (!line[0]) continue;
        if (first) { first = false; continue; }
        char *fields[8];
        int n = csv_split(line, fields, 8);
        if (n < 5) continue;
        int slot = total % max;
        inv_hist_t *h = &hist[slot];
        memset(h, 0, sizeof(*h));
        snprintf(h->stamp, sizeof(h->stamp), "%s", trim(fields[0]));
        snprintf(h->action, sizeof(h->action), "%s", trim(fields[1]));
        snprintf(h->model, sizeof(h->model), "%s", trim(fields[2]));
        snprintf(h->product_no, sizeof(h->product_no), "%s", trim(fields[3]));
        h->qty = (int32_t)strtol(trim(fields[4]), NULL, 10);
        total++;
    }
    fclose(f);

    int count = total < max ? total : max;
    if (total > max && count > 1) {
        inv_hist_t *tmp = malloc(sizeof(inv_hist_t) * (size_t)count);
        if (tmp) {
            int oldest = total % max;
            for (int i = 0; i < count; ++i) tmp[i] = hist[(oldest + i) % max];
            memcpy(hist, tmp, sizeof(inv_hist_t) * (size_t)count);
            free(tmp);
        }
    }
    return count;
}
