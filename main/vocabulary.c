#include "vocabulary.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sd_monitor.h"

static const char *TAG = "vocab";

#define VOCAB_NVS_NS            "vocab"
#define VOCAB_DEFAULT_DAILY     20
#define VOCAB_SESSION_MAX       80
#define VOCAB_LINE_MAX          1024
#define VOCAB_PROGRESS_MAGIC    0x56435031u /* VCP1 */
#define VOCAB_PROGRESS_VERSION  2u
#define VOCAB_REC_MASTERED      0x01u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t word_count;
    uint32_t correct_total;
    uint32_t wrong_total;
    uint32_t learned_total;
    uint32_t last_day;
    uint16_t today_done;
    uint16_t reserved;
} progress_hdr_t;

typedef struct {
    uint32_t due_day;
    uint16_t seen;
    uint8_t box;
    uint8_t flags;
} progress_rec_v1_t;

typedef struct {
    uint32_t due_day;
    uint32_t last_study_day;   // unique daily-plan accounting / resume guard
    uint16_t seen;
    uint8_t box;
    uint8_t flags;
} progress_rec_t;

typedef struct {
    uint32_t word_index;
    long file_offset;
} session_entry_t;

typedef struct {
    int word;
    int phonetic;
    int meaning;
    int example;
    int max_col;
    bool valid;
} csv_cols_t;

static char s_active_file[VOCAB_FILENAME_MAX];
static uint32_t s_active_count;
static uint16_t s_daily_target = VOCAB_DEFAULT_DAILY;
static session_entry_t s_session[VOCAB_SESSION_MAX];
static int s_session_count;
static int s_session_pos;
static vocab_mode_t s_session_mode;
static bool s_session_answered;
static vocab_word_t s_current_cache;
static bool s_current_valid;
static vocab_stats_t s_stats_cache;
static bool s_stats_valid;
static uint32_t s_stats_cache_day;
static uint32_t s_last_trusted_day;

static volatile vocab_open_status_t s_open = {
    .state = VOCAB_OPEN_IDLE,
    .mode = VOCAB_MODE_LEARN,
};

static volatile vocab_download_status_t s_dl = {
    .state = VOCAB_DL_IDLE,
    .catalog_index = -1,
};

static const vocab_catalog_item_t CATALOG[] = {
    {
        .id = "cet4",
        .title = "CET-4 Core",
        .category = "College English",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/cet4.json",
        .filename = "cet4.csv",
    },
    {
        .id = "cet4_high_freq",
        .title = "CET-4 High Freq",
        .category = "College English",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/cet4_high_freq.json",
        .filename = "cet4_high_freq.csv",
    },
    {
        .id = "cet6",
        .title = "CET-6 Core",
        .category = "College English",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/cet6.json",
        .filename = "cet6.csv",
    },
    {
        .id = "cet6_high_freq",
        .title = "CET-6 High Freq",
        .category = "College English",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/cet6_high_freq.json",
        .filename = "cet6_high_freq.csv",
    },
    {
        .id = "kaoyan",
        .title = "Kaoyan Core",
        .category = "Graduate Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/kaoyan.json",
        .filename = "kaoyan.csv",
    },
    {
        .id = "kaoyan_high_freq",
        .title = "Kaoyan High Freq",
        .category = "Graduate Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/kaoyan_high_freq.json",
        .filename = "kaoyan_high_freq.csv",
    },
    {
        .id = "toefl",
        .title = "TOEFL Core",
        .category = "Overseas Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/toefl.json",
        .filename = "toefl.csv",
    },
    {
        .id = "toefl_high_freq",
        .title = "TOEFL High Freq",
        .category = "Overseas Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/toefl_high_freq.json",
        .filename = "toefl_high_freq.csv",
    },
    {
        .id = "ielts_core",
        .title = "IELTS Core",
        .category = "Overseas Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/ielts_core.json",
        .filename = "ielts_core.csv",
    },
    {
        .id = "ielts_basic",
        .title = "IELTS Basic",
        .category = "Overseas Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/ielts_basic.json",
        .filename = "ielts_basic.csv",
    },
    {
        .id = "ielts_advanced",
        .title = "IELTS Advanced",
        .category = "Overseas Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/ielts_advanced.json",
        .filename = "ielts_advanced.csv",
    },
    {
        .id = "gre",
        .title = "GRE Core",
        .category = "Graduate Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/gre.json",
        .filename = "gre.csv",
    },
    {
        .id = "gmat",
        .title = "GMAT Core",
        .category = "Graduate Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/gmat.json",
        .filename = "gmat.csv",
    },
    {
        .id = "sat",
        .title = "SAT Core",
        .category = "Graduate Exam",
        .url = "https://raw.githubusercontent.com/grhliu/wordtyper-vocabularies/refs/heads/main/vocabularies/sat.json",
        .filename = "sat.csv",
    },
};

static bool ends_with_ci(const char *s, const char *suffix);
static bool json_to_csv(const char *json_path, const char *csv_tmp_path, uint32_t *out_words, volatile vocab_open_status_t *status);

// Convert a manually downloaded foo.json filename into foo.csv safely.
static bool json_filename_to_csv(const char *json_name, char *csv_name, size_t csv_name_len)
{
    if (!json_name || !csv_name || csv_name_len == 0 || !ends_with_ci(json_name, ".json")) return false;
    size_t n = strlen(json_name);
    if (n < 5) return false;
    size_t stem = n - 5;
    if (stem + 5 > csv_name_len) return false; // ".csv" + NUL
    memcpy(csv_name, json_name, stem);
    memcpy(csv_name + stem, ".csv", 5);
    return true;
}

static bool ends_with_ci(const char *s, const char *suffix)
{
    if (!s || !suffix) return false;
    size_t ns = strlen(s), nx = strlen(suffix);
    if (ns < nx) return false;
    s += ns - nx;
    for (size_t i = 0; i < nx; ++i) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)suffix[i])) return false;
    }
    return true;
}

static void pretty_title_from_filename(const char *filename, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!filename) return;
    size_t n = strlen(filename);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, filename, n);
    out[n] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    for (char *p = out; *p; ++p) {
        if (*p == '_' || *p == '-') *p = ' ';
    }
}

static bool dir_exists(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return false;
    closedir(d);
    return true;
}

static bool ensure_dir(const char *path)
{
    if (dir_exists(path)) return true;
    if (mkdir(path, 0777) == 0) return true;
    return errno == EEXIST;
}

bool vocab_ensure_dirs(void)
{
    const sdmon_status_t *sd = sd_monitor_get_status();
    if (!sd || !sd->mounted) return false;
    return ensure_dir("/sdcard/vocabulary") &&
           ensure_dir(VOCAB_BOOK_DIR) &&
           ensure_dir(VOCAB_PROGRESS_DIR) &&
           ensure_dir(VOCAB_CACHE_DIR);
}

static uint32_t civil_day_id(int y, unsigned m, unsigned d)
{
    // Howard Hinnant's civil-date conversion. Produces a monotonically
    // increasing day id without depending on UTC/DST arithmetic.
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned mp = (unsigned)((int)m + (m > 2 ? -3 : 9));
    const unsigned doy = (153 * mp + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + (int)doe + 719468);
}

static void save_trusted_day(uint32_t day)
{
    if (!day || day == s_last_trusted_day) return;
    s_last_trusted_day = day;
    nvs_handle_t h;
    if (nvs_open(VOCAB_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_u32(h, "clock_day", day);
        (void)nvs_commit(h);
        nvs_close(h);
    }
}

static uint32_t today_day(void)
{
    time_t now = 0;
    time(&now);
    if (now >= (time_t)1577836800) {
        struct tm tmv;
        localtime_r(&now, &tmv);
        if (tmv.tm_year >= 120) {
            uint32_t day = civil_day_id(tmv.tm_year + 1900, (unsigned)tmv.tm_mon + 1U, (unsigned)tmv.tm_mday);
            save_trusted_day(day);
            return day;
        }
    }
    // Cold boot without SNTP: keep the last known day. This deliberately does
    // NOT advance the plan until a real clock arrives, avoiding false resets.
    return s_last_trusted_day;
}

bool vocab_daily_date_known(void)
{
    return today_day() != 0;
}

static int str_icmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static char *trim(char *s)
{
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) ++s;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static size_t utf8_char_len(const unsigned char *p)
{
    if (!p || !*p) return 0;
    if (p[0] < 0x80) return 1;
    if ((p[0] & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) return 2;
    if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2] &&
        (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) return 3;
    if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3] &&
        (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) return 4;
    return 0;
}

static void utf8_copy_safe(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    dst[0] = '\0';
    if (!src) return;
    const unsigned char *p = (const unsigned char *)src;
    if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) p += 3;
    size_t used = 0;
    while (*p && used + 1 < dst_len) {
        size_t n = utf8_char_len(p);
        if (n == 0) {
            if (used + 1 < dst_len) dst[used++] = '?';
            ++p;
            continue;
        }
        if (n == 1 && p[0] < 0x20 && p[0] != '\t') {
            ++p;
            continue;
        }
        if (used + n >= dst_len) break;
        memcpy(dst + used, p, n);
        used += n;
        p += n;
    }
    dst[used] = '\0';
}

static void utf8_append_safe(char *dst, size_t dst_len, const char *src)
{
    if (!dst || !dst_len || !src) return;
    size_t used = strlen(dst);
    if (used >= dst_len - 1) return;
    char tmp[VOCAB_MEANING_MAX];
    utf8_copy_safe(tmp, sizeof(tmp), src);
    const unsigned char *p = (const unsigned char *)tmp;
    while (*p) {
        size_t n = utf8_char_len(p);
        if (!n) { ++p; continue; }
        if (used + n >= dst_len) break;
        memcpy(dst + used, p, n);
        used += n;
        p += n;
    }
    dst[used] = '\0';
}

// In-place CSV parser. Handles quoted fields, commas inside quotes and doubled quotes.
static int csv_split(char *line, char *fields[], int max_fields)
{
    if (!line || !fields || max_fields <= 0) return 0;
    int count = 0;
    char *src = line;
    char *dst = line;

    while (*src && count < max_fields) {
        fields[count++] = dst;
        bool quoted = false;
        if (*src == '"') {
            quoted = true;
            ++src;
        }
        while (*src) {
            if (quoted) {
                if (*src == '"') {
                    if (src[1] == '"') {
                        *dst++ = '"';
                        src += 2;
                        continue;
                    }
                    ++src;
                    quoted = false;
                    while (*src == ' ' || *src == '\t') ++src;
                    if (*src == ',') ++src;
                    break;
                }
                *dst++ = *src++;
            } else {
                if (*src == ',') {
                    ++src;
                    break;
                }
                *dst++ = *src++;
            }
        }
        *dst++ = '\0';
    }
    return count;
}

static csv_cols_t detect_cols(char *header)
{
    csv_cols_t c = { .word = -1, .phonetic = -1, .meaning = -1, .example = -1, .max_col = -1, .valid = false };
    char *f[24];
    int n = csv_split(header, f, 24);
    for (int i = 0; i < n; ++i) {
        char *x = trim(f[i]);
        if (str_icmp(x, "word") == 0 || str_icmp(x, "headword") == 0) c.word = i;
        else if (str_icmp(x, "phonetic") == 0 || str_icmp(x, "ipa") == 0) c.phonetic = i;
        else if (str_icmp(x, "meaning") == 0 || str_icmp(x, "meaning_cn") == 0 || str_icmp(x, "translation") == 0) c.meaning = i;
        else if (str_icmp(x, "example") == 0 || str_icmp(x, "examples") == 0 || str_icmp(x, "sentence") == 0) c.example = i;
    }
    if (c.word >= 0 && c.meaning >= 0) {
        c.valid = true;
        c.max_col = c.word;
        if (c.phonetic > c.max_col) c.max_col = c.phonetic;
        if (c.meaning > c.max_col) c.max_col = c.meaning;
        if (c.example > c.max_col) c.max_col = c.example;
    }
    return c;
}

static bool load_cols(FILE *f, csv_cols_t *out)
{
    if (!f || !out) return false;
    rewind(f);
    char line[VOCAB_LINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;

        // Many Windows/Excel-generated UTF-8 CSV files start with a BOM.
        // Ignore it so "\xEF\xBB\xBFword" is correctly detected as "word".
        const unsigned char *u = (const unsigned char *)p;
        if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) p += 3;

        char copy[VOCAB_LINE_MAX];
        snprintf(copy, sizeof(copy), "%s", p);
        *out = detect_cols(copy);
        return out->valid;
    }
    return false;
}

static bool parse_word_line(char *line, const csv_cols_t *c, vocab_word_t *out)
{
    if (!line || !c || !c->valid || !out) return false;
    char *f[24];
    int n = csv_split(line, f, 24);
    if (n <= c->max_col) return false;
    const char *word = trim(f[c->word]);
    const char *meaning = trim(f[c->meaning]);
    if (!word[0] || !meaning[0]) return false;

    memset(out, 0, sizeof(*out));
    utf8_copy_safe(out->word, sizeof(out->word), word);
    if (c->phonetic >= 0 && c->phonetic < n)
        utf8_copy_safe(out->phonetic, sizeof(out->phonetic), trim(f[c->phonetic]));
    utf8_copy_safe(out->meaning, sizeof(out->meaning), meaning);
    if (c->example >= 0 && c->example < n)
        utf8_copy_safe(out->example, sizeof(out->example), trim(f[c->example]));
    return true;
}

static void book_path(const char *filename, char *path, size_t len)
{
    snprintf(path, len, "%s/%s", VOCAB_BOOK_DIR, filename ? filename : "");
}

static void progress_path(const char *filename, char *path, size_t len)
{
    char base[VOCAB_FILENAME_MAX];
    snprintf(base, sizeof(base), "%s", filename ? filename : "book");
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    snprintf(path, len, "%s/%s.vcp", VOCAB_PROGRESS_DIR, base);
}

static uint32_t count_book_words_ex(const char *filename, volatile vocab_open_status_t *status)
{
    char path[160];
    book_path(filename, path, sizeof(path));
    struct stat st;
    size_t total = (stat(path, &st) == 0 && st.st_size > 0) ? (size_t)st.st_size : 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    csv_cols_t cols;
    if (!load_cols(f, &cols)) {
        fclose(f);
        return 0;
    }

    rewind(f);
    char line[VOCAB_LINE_MAX];
    bool header_seen = false;
    uint32_t count = 0;
    uint32_t lines = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        if (!header_seen) { header_seen = true; continue; }
        vocab_word_t tmp;
        if (parse_word_line(p, &cols, &tmp)) count++;
        if (status && (++lines & 0x3Fu) == 0) {
            long pos = ftell(f);
            status->bytes_done = pos > 0 ? (size_t)pos : 0;
            status->bytes_total = total;
            status->words_done = count;
            vTaskDelay(1);
        }
    }
    if (status) { status->bytes_done = total; status->bytes_total = total; status->words_done = count; }
    fclose(f);
    return count;
}

static uint32_t count_book_words(const char *filename)
{
    return count_book_words_ex(filename, NULL);
}

static bool create_progress_file(const char *filename, uint32_t word_count)
{
    char path[160];
    progress_path(filename, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    progress_hdr_t h = {
        .magic = VOCAB_PROGRESS_MAGIC,
        .version = VOCAB_PROGRESS_VERSION,
        .record_size = sizeof(progress_rec_t),
        .word_count = word_count,
        .last_day = today_day(),
    };
    if (fwrite(&h, 1, sizeof(h), f) != sizeof(h)) {
        fclose(f);
        return false;
    }
    progress_rec_t zeros[64] = {0};
    uint32_t left = word_count;
    while (left) {
        uint32_t n = left > 64 ? 64 : left;
        if (fwrite(zeros, sizeof(progress_rec_t), n, f) != n) {
            fclose(f);
            return false;
        }
        left -= n;
        vTaskDelay(1);
    }
    fclose(f);
    return true;
}

static bool migrate_progress_v1(const char *filename, const progress_hdr_t *old_hdr)
{
    if (!filename || !old_hdr || old_hdr->version != 1u || old_hdr->record_size != sizeof(progress_rec_v1_t)) return false;
    char path[160], tmp[176];
    progress_path(filename, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.migrate", path);
    FILE *in = fopen(path, "rb");
    FILE *out = fopen(tmp, "wb");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); unlink(tmp); return false; }
    progress_hdr_t skip;
    if (fread(&skip, 1, sizeof(skip), in) != sizeof(skip)) { fclose(in); fclose(out); unlink(tmp); return false; }
    progress_hdr_t nh = *old_hdr;
    nh.version = VOCAB_PROGRESS_VERSION;
    nh.record_size = sizeof(progress_rec_t);
    nh.today_done = 0; // v1 had no per-word day marker, so avoid double counting after migration
    if (fwrite(&nh, 1, sizeof(nh), out) != sizeof(nh)) { fclose(in); fclose(out); unlink(tmp); return false; }
    for (uint32_t i = 0; i < old_hdr->word_count; ++i) {
        progress_rec_v1_t r1;
        progress_rec_t r2 = {0};
        if (fread(&r1, 1, sizeof(r1), in) != sizeof(r1)) { fclose(in); fclose(out); unlink(tmp); return false; }
        r2.due_day = r1.due_day; r2.seen = r1.seen; r2.box = r1.box; r2.flags = r1.flags;
        if (fwrite(&r2, 1, sizeof(r2), out) != sizeof(r2)) { fclose(in); fclose(out); unlink(tmp); return false; }
    }
    bool ok = fflush(out) == 0 && !ferror(out);
    fclose(in); fclose(out);
    if (!ok) { unlink(tmp); return false; }
    unlink(path);
    if (rename(tmp, path) != 0) { unlink(tmp); return false; }
    ESP_LOGI(TAG, "migrated progress v1 -> v2: %s", filename);
    return true;
}

static bool open_progress(const char *mode, FILE **out, progress_hdr_t *hdr)
{
    if (!s_active_file[0] || !out || !hdr) return false;
    char path[160];
    progress_path(s_active_file, path, sizeof(path));
    FILE *f = fopen(path, mode);
    if (!f) {
        if (!create_progress_file(s_active_file, s_active_count)) return false;
        f = fopen(path, mode);
        if (!f) return false;
    }
    progress_hdr_t h;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h)) {
        fclose(f);
        if (!create_progress_file(s_active_file, s_active_count)) return false;
        f = fopen(path, mode);
        if (!f || fread(&h, 1, sizeof(h), f) != sizeof(h)) { if (f) fclose(f); return false; }
    } else if (h.magic == VOCAB_PROGRESS_MAGIC && h.version == 1u &&
               h.record_size == sizeof(progress_rec_v1_t) && h.word_count == s_active_count) {
        fclose(f);
        if (!migrate_progress_v1(s_active_file, &h)) return false;
        f = fopen(path, mode);
        if (!f || fread(&h, 1, sizeof(h), f) != sizeof(h)) { if (f) fclose(f); return false; }
    } else if (h.magic != VOCAB_PROGRESS_MAGIC ||
               h.version != VOCAB_PROGRESS_VERSION ||
               h.record_size != sizeof(progress_rec_t) ||
               h.word_count != s_active_count) {
        fclose(f);
        if (!create_progress_file(s_active_file, s_active_count)) return false;
        f = fopen(path, mode);
        if (!f || fread(&h, 1, sizeof(h), f) != sizeof(h)) { if (f) fclose(f); return false; }
    }
    *out = f;
    *hdr = h;
    return true;
}

static bool read_rec(FILE *f, uint32_t idx, progress_rec_t *r)
{
    if (!f || !r || idx >= s_active_count) return false;
    long off = (long)sizeof(progress_hdr_t) + (long)idx * (long)sizeof(progress_rec_t);
    if (fseek(f, off, SEEK_SET) != 0) return false;
    return fread(r, 1, sizeof(*r), f) == sizeof(*r);
}

static bool write_rec(FILE *f, uint32_t idx, const progress_rec_t *r)
{
    if (!f || !r || idx >= s_active_count) return false;
    long off = (long)sizeof(progress_hdr_t) + (long)idx * (long)sizeof(progress_rec_t);
    if (fseek(f, off, SEEK_SET) != 0) return false;
    return fwrite(r, 1, sizeof(*r), f) == sizeof(*r);
}

static bool write_hdr(FILE *f, const progress_hdr_t *h)
{
    if (!f || !h) return false;
    if (fseek(f, 0, SEEK_SET) != 0) return false;
    return fwrite(h, 1, sizeof(*h), f) == sizeof(*h);
}

static bool refresh_stats(void)
{
    memset(&s_stats_cache, 0, sizeof(s_stats_cache));
    s_stats_cache.daily_target = s_daily_target;
    s_stats_cache.total_words = s_active_count;
    s_stats_cache.new_words = s_active_count;
    if (!s_active_file[0] || !s_active_count) { s_stats_cache_day = today_day(); s_stats_valid = true; return false; }

    FILE *f; progress_hdr_t h;
    if (!open_progress("rb+", &f, &h)) { s_stats_valid = false; return false; }
    uint32_t day = today_day();
    if (day && h.last_day != day) { h.last_day = day; h.today_done = 0; (void)write_hdr(f, &h); fflush(f); }

    uint32_t due = 0;
    if (fseek(f, (long)sizeof(progress_hdr_t), SEEK_SET) == 0) {
        progress_rec_t block[64];
        uint32_t left = h.word_count;
        while (left) {
            size_t want = left > 64 ? 64 : left;
            size_t got = fread(block, sizeof(progress_rec_t), want, f);
            for (size_t i = 0; i < got; ++i) {
                if (block[i].box > 0 && (!day || block[i].due_day <= day) &&
                    (!day || block[i].last_study_day != day)) due++;
            }
            if (got != want) break;
            left -= (uint32_t)got;
        }
    }
    fclose(f);

    s_stats_cache.learned_words = h.learned_total > h.word_count ? h.word_count : h.learned_total;
    s_stats_cache.new_words = h.word_count - s_stats_cache.learned_words;
    s_stats_cache.correct_total = h.correct_total;
    s_stats_cache.wrong_total = h.wrong_total;
    s_stats_cache.today_done = h.today_done;
    s_stats_cache.due_words = due;
    s_stats_cache.daily_target = s_daily_target;
    s_stats_cache_day = day;
    s_stats_valid = true;
    return true;
}

static esp_err_t load_nvs_settings(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(VOCAB_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    uint16_t daily = 0;
    if (nvs_get_u16(h, "daily", &daily) == ESP_OK && daily >= 5 && daily <= 100) {
        s_daily_target = daily;
    } else {
        s_daily_target = VOCAB_DEFAULT_DAILY;
        (void)nvs_set_u16(h, "daily", s_daily_target);
    }
    size_t len = sizeof(s_active_file);
    if (nvs_get_str(h, "active", s_active_file, &len) != ESP_OK) s_active_file[0] = '\0';
    if (nvs_get_u32(h, "clock_day", &s_last_trusted_day) != ESP_OK) s_last_trusted_day = 0;
    (void)nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t vocab_init(void)
{
    memset(&s_stats_cache, 0, sizeof(s_stats_cache));
    s_active_count = 0;
    s_session_count = 0;
    s_session_pos = 0;
    s_current_valid = false;
    s_stats_valid = false;
    s_stats_cache_day = 0;
    s_dl.state = VOCAB_DL_IDLE;
    s_dl.catalog_index = -1;
    s_open.state = VOCAB_OPEN_IDLE;
    s_open.filename[0] = '\0';
    s_open.message[0] = '\0';
    return load_nvs_settings();
}

int vocab_list_books(vocab_book_info_t *out, int max_items)
{
    if (!out || max_items <= 0) return 0;
    if (!vocab_ensure_dirs()) return 0;
    DIR *d = opendir(VOCAB_BOOK_DIR);
    if (!d) return 0;
    int count = 0;
    struct dirent *ent;
    while (count < max_items && (ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        bool is_csv = ends_with_ci(ent->d_name, ".csv");
        bool is_json = ends_with_ci(ent->d_name, ".json");
        if (!is_csv && !is_json) continue;

        size_t fn_len = strlen(ent->d_name);
        if (fn_len >= VOCAB_FILENAME_MAX) {
            ESP_LOGW(TAG, "Skipping overlong wordbook filename: %s", ent->d_name);
            continue;
        }

        // If foo.csv already exists, hide foo.json from the menu. The JSON is
        // still kept on SD as the user's original download, but the normalized
        // CSV becomes the canonical book after first import.
        if (is_json) {
            char csv_name[VOCAB_FILENAME_MAX];
            char csv_path[160];
            struct stat csv_st;
            if (json_filename_to_csv(ent->d_name, csv_name, sizeof(csv_name))) {
                book_path(csv_name, csv_path, sizeof(csv_path));
                if (stat(csv_path, &csv_st) == 0 && S_ISREG(csv_st.st_mode) && csv_st.st_size > 32) continue;
            }
        }

        vocab_book_info_t *b = &out[count];
        memset(b, 0, sizeof(*b));
        memcpy(b->filename, ent->d_name, fn_len + 1);
        pretty_title_from_filename(b->filename, b->title, sizeof(b->title));
        b->selected = is_csv && (strcmp(b->filename, s_active_file) == 0);
        if (b->selected) b->word_count = s_active_count;
        count++;
    }
    closedir(d);
    return count;
}

static esp_err_t select_csv_book(const char *filename, uint32_t count_hint, volatile vocab_open_status_t *status)
{
    if (!filename || !*filename || strchr(filename, '/') || strstr(filename, "..") || strlen(filename) >= VOCAB_FILENAME_MAX) return ESP_ERR_INVALID_ARG;
    char path[160]; book_path(filename, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return ESP_ERR_NOT_FOUND;
    uint32_t count = count_hint ? count_hint : count_book_words_ex(filename, status);
    if (!count) return ESP_ERR_INVALID_RESPONSE;
    memcpy(s_active_file, filename, strlen(filename) + 1);
    s_active_count = count; s_session_count = 0; s_session_pos = 0; s_current_valid = false; s_stats_valid = false;
    nvs_handle_t h;
    if (nvs_open(VOCAB_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_str(h, "active", s_active_file); (void)nvs_commit(h); nvs_close(h);
    }
    FILE *pf; progress_hdr_t ph;
    if (!open_progress("rb+", &pf, &ph)) return ESP_FAIL;
    fclose(pf);
    refresh_stats();
    ESP_LOGI(TAG, "selected book %s (%lu words)", s_active_file, (unsigned long)s_active_count);
    return ESP_OK;
}

esp_err_t vocab_select_book(const char *filename)
{
    if (!filename || !*filename || strchr(filename, '/') || strstr(filename, "..")) return ESP_ERR_INVALID_ARG;
    if (strlen(filename) >= VOCAB_FILENAME_MAX) return ESP_ERR_INVALID_ARG;
    if (!vocab_ensure_dirs()) return ESP_ERR_INVALID_STATE;

    char selected_name[VOCAB_FILENAME_MAX];
    memcpy(selected_name, filename, strlen(filename) + 1);

    // Manual-download path: a compatible WordTyper JSON can be copied directly
    // to /sdcard/vocabulary/books. On first tap in WORDBOOKS, normalize it to
    // the firmware's CSV schema, then continue through the normal select/start
    // path. This keeps online download optional rather than required.
    if (ends_with_ci(selected_name, ".json")) {
        char csv_name[VOCAB_FILENAME_MAX];
        char json_path[160], csv_tmp[176], csv_final[160];
        uint32_t imported_words = 0;
        if (!json_filename_to_csv(selected_name, csv_name, sizeof(csv_name))) return ESP_ERR_INVALID_ARG;

        book_path(selected_name, json_path, sizeof(json_path));
        book_path(csv_name, csv_final, sizeof(csv_final));
        snprintf(csv_tmp, sizeof(csv_tmp), "%s/%s.tmp", VOCAB_BOOK_DIR, csv_name);

        struct stat st_json;
        if (stat(json_path, &st_json) != 0 || !S_ISREG(st_json.st_mode)) return ESP_ERR_NOT_FOUND;
        unlink(csv_tmp);
        if (!json_to_csv(json_path, csv_tmp, &imported_words, NULL)) {
            unlink(csv_tmp);
            ESP_LOGE(TAG, "local JSON import failed: %s", selected_name);
            return ESP_ERR_INVALID_RESPONSE;
        }
        unlink(csv_final);
        if (rename(csv_tmp, csv_final) != 0) {
            unlink(csv_tmp);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "imported local JSON %s -> %s (%lu words)",
                 selected_name, csv_name, (unsigned long)imported_words);
        memcpy(selected_name, csv_name, strlen(csv_name) + 1);
    }

    return select_csv_book(selected_name, 0, NULL);
}

bool vocab_has_active_book(void)
{
    if (!s_active_file[0]) return false;
    if (s_active_count) return true;
    const sdmon_status_t *sd = sd_monitor_get_status();
    if (!sd || !sd->mounted) return false;
    s_active_count = count_book_words(s_active_file);
    return s_active_count > 0;
}

const char *vocab_active_book(void)
{
    return s_active_file;
}

uint32_t vocab_active_word_count(void)
{
    (void)vocab_has_active_book();
    return s_active_count;
}

uint16_t vocab_get_daily_target(void)
{
    return s_daily_target;
}

esp_err_t vocab_set_daily_target(uint16_t target)
{
    if (target < 5) target = 5;
    if (target > 100) target = 100;
    s_daily_target = target;
    s_stats_valid = false;
    nvs_handle_t h;
    esp_err_t err = nvs_open(VOCAB_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(h, "daily", target);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool vocab_get_stats(vocab_stats_t *out)
{
    if (!out) return false;
    (void)vocab_has_active_book();
    uint32_t day = today_day();
    if (s_stats_valid && day != s_stats_cache_day) s_stats_valid = false;
    if (!s_stats_valid) refresh_stats();
    *out = s_stats_cache;
    return s_active_count > 0;
}

esp_err_t vocab_reset_active_progress(void)
{
    if (!vocab_has_active_book()) return ESP_ERR_INVALID_STATE;
    char path[160];
    progress_path(s_active_file, path, sizeof(path));
    unlink(path);
    if (!create_progress_file(s_active_file, s_active_count)) return ESP_FAIL;
    s_session_count = 0;
    s_session_pos = 0;
    s_current_valid = false;
    s_stats_valid = false;
    refresh_stats();
    return ESP_OK;
}

static bool queue_contains(uint32_t idx)
{
    for (int i = 0; i < s_session_count; ++i) {
        if (s_session[i].word_index == idx) return true;
    }
    return false;
}

static void queue_add(uint32_t idx)
{
    if (s_session_count >= VOCAB_SESSION_MAX || queue_contains(idx)) return;
    s_session[s_session_count].word_index = idx;
    s_session[s_session_count].file_offset = -1;
    s_session_count++;
}

static bool build_session_offsets(void)
{
    if (!s_active_file[0] || s_session_count <= 0) return false;
    char path[160];
    book_path(s_active_file, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return false;
    csv_cols_t cols;
    if (!load_cols(f, &cols)) {
        fclose(f);
        return false;
    }
    rewind(f);
    char line[VOCAB_LINE_MAX];
    bool header_seen = false;
    uint32_t word_index = 0;
    int mapped = 0;
    while (1) {
        long off = ftell(f);
        if (!fgets(line, sizeof(line), f)) break;
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        if (!header_seen) {
            header_seen = true;
            continue;
        }
        vocab_word_t tmp;
        if (!parse_word_line(p, &cols, &tmp)) continue;
        for (int i = 0; i < s_session_count; ++i) {
            if (s_session[i].word_index == word_index) {
                s_session[i].file_offset = off;
                mapped++;
                break;
            }
        }
        word_index++;
        if (mapped >= s_session_count) break;
    }
    fclose(f);
    return mapped == s_session_count;
}

static bool seek_progress_records(FILE *f)
{
    return f && fseek(f, (long)sizeof(progress_hdr_t), SEEK_SET) == 0;
}

esp_err_t vocab_start_session(vocab_mode_t mode)
{
    if (!vocab_has_active_book()) return ESP_ERR_INVALID_STATE;
    s_session_count = 0; s_session_pos = 0; s_session_mode = mode; s_session_answered = false; s_current_valid = false;

    FILE *f; progress_hdr_t h;
    if (!open_progress("rb+", &f, &h)) return ESP_FAIL;
    uint32_t day = today_day();
    if (day && h.last_day != day) { h.last_day = day; h.today_done = 0; (void)write_hdr(f, &h); fflush(f); }

    int daily_goal = s_daily_target;
    if (daily_goal < 5) daily_goal = 5;
    if (daily_goal > 100) daily_goal = 100;
    int target = daily_goal > VOCAB_SESSION_MAX ? VOCAB_SESSION_MAX : daily_goal;
    if (mode == VOCAB_MODE_LEARN && day) {
        if (h.today_done >= (uint16_t)daily_goal) { fclose(f); return ESP_ERR_NOT_FOUND; }
        int remaining = daily_goal - h.today_done;
        target = remaining > VOCAB_SESSION_MAX ? VOCAB_SESSION_MAX : remaining;
        if (target < 1) target = 1;
    }

    // Pass 1: due reviews not already completed today. Sequential reads avoid
    // thousands of tiny FAT fseek operations on large books.
    if (seek_progress_records(f)) {
        for (uint32_t i = 0; i < h.word_count && s_session_count < target; ++i) {
            progress_rec_t r; if (fread(&r, 1, sizeof(r), f) != sizeof(r)) break;
            if (r.box > 0 && (!day || r.due_day <= day) && (!day || r.last_study_day != day)) queue_add(i);
        }
    }

    if (mode == VOCAB_MODE_LEARN) {
        if (s_session_count < target && seek_progress_records(f)) {
            for (uint32_t i = 0; i < h.word_count && s_session_count < target; ++i) {
                progress_rec_t r; if (fread(&r, 1, sizeof(r), f) != sizeof(r)) break;
                if (r.box == 0 && (!day || r.last_study_day != day)) queue_add(i);
            }
        }
    } else {
        // Dictation is practice for words that have already been learned. It
        // no longer consumes brand-new daily words on a fresh book.
        if (s_session_count < target && seek_progress_records(f)) {
            for (uint32_t i = 0; i < h.word_count && s_session_count < target; ++i) {
                progress_rec_t r; if (fread(&r, 1, sizeof(r), f) != sizeof(r)) break;
                if (r.box > 0) queue_add(i);
            }
        }
    }
    fclose(f);

    if (s_session_count == 0) return ESP_ERR_NOT_FOUND;
    if (!build_session_offsets()) { s_session_count = 0; return ESP_FAIL; }
    return ESP_OK;
}

int vocab_session_count(void) { return s_session_count; }
int vocab_session_position(void) { return s_session_pos; }
bool vocab_session_finished(void) { return s_session_count <= 0 || s_session_pos >= s_session_count; }

bool vocab_get_current(vocab_word_t *out)
{
    if (!out || vocab_session_finished()) return false;
    if (s_current_valid) {
        *out = s_current_cache;
        return true;
    }
    session_entry_t *e = &s_session[s_session_pos];
    if (e->file_offset < 0) return false;
    char path[160];
    book_path(s_active_file, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return false;
    csv_cols_t cols;
    if (!load_cols(f, &cols)) {
        fclose(f);
        return false;
    }
    if (fseek(f, e->file_offset, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    char line[VOCAB_LINE_MAX];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    char *p = trim(line);
    if (!parse_word_line(p, &cols, &s_current_cache)) return false;
    s_current_cache.word_index = e->word_index;
    s_current_valid = true;
    *out = s_current_cache;
    return true;
}

bool vocab_mark_current(bool correct)
{
    if (vocab_session_finished()) return false;
    if (s_session_answered) return true;

    FILE *f;
    progress_hdr_t h;
    if (!open_progress("rb+", &f, &h)) return false;
    uint32_t idx = s_session[s_session_pos].word_index;
    progress_rec_t r;
    if (!read_rec(f, idx, &r)) {
        fclose(f);
        return false;
    }
    bool was_new = (r.box == 0);
    uint32_t day = today_day();
    static const uint8_t intervals[] = {0, 0, 1, 3, 7, 14};

    r.seen++;
    if (correct) {
        if (r.box < 5) r.box++;
        uint8_t b = r.box > 5 ? 5 : r.box;
        r.due_day = day ? day + intervals[b] : 0;
        if (r.box >= 5) r.flags |= VOCAB_REC_MASTERED;
        h.correct_total++;
    } else {
        r.box = 1;
        r.flags &= (uint8_t)~VOCAB_REC_MASTERED;
        r.due_day = day;
        h.wrong_total++;
    }
    if (was_new && h.learned_total < h.word_count) h.learned_total++;
    if (day && s_session_mode == VOCAB_MODE_LEARN) {
        if (h.last_day != day) { h.last_day = day; h.today_done = 0; }
        if (r.last_study_day != day) {
            r.last_study_day = day;
            if (h.today_done < 65535) h.today_done++;
        }
    }

    bool ok = write_rec(f, idx, &r) && write_hdr(f, &h);
    fflush(f);
    fclose(f);
    if (ok) {
        s_session_answered = true;
        s_stats_valid = false;
    }
    return ok;
}

bool vocab_next(void)
{
    if (s_session_count <= 0) return false;
    if (s_session_pos < s_session_count) s_session_pos++;
    s_session_answered = false;
    s_current_valid = false;
    return !vocab_session_finished();
}

bool vocab_check_dictation(const char *answer)
{
    if (!answer) return false;
    vocab_word_t w;
    if (!vocab_get_current(&w)) return false;
    char a[96];
    snprintf(a, sizeof(a), "%s", answer);
    char *t = trim(a);
    return str_icmp(t, w.word) == 0;
}

static void open_set(vocab_open_state_t state, const char *msg)
{
    // Publish text first and state last so the UI never observes a new phase
    // paired with stale text from the previous phase.
    if (msg) snprintf((char *)s_open.message, sizeof(s_open.message), "%.63s", msg);
    s_open.state = state;
}

static void open_book_task(void *arg)
{
    (void)arg;
    char selected[VOCAB_FILENAME_MAX];
    memcpy(selected, (const char *)s_open.filename, sizeof(selected));
    selected[sizeof(selected) - 1] = '\0';
    vocab_mode_t mode = s_open.mode;

    if (!vocab_ensure_dirs()) { open_set(VOCAB_OPEN_ERROR, "SD card not mounted"); vTaskDelete(NULL); return; }

    uint32_t count_hint = 0;
    if (ends_with_ci(selected, ".json")) {
        char csv_name[VOCAB_FILENAME_MAX], json_path[160], csv_tmp[176], csv_final[160];
        if (!json_filename_to_csv(selected, csv_name, sizeof(csv_name))) { open_set(VOCAB_OPEN_ERROR, "Bad JSON filename"); vTaskDelete(NULL); return; }
        book_path(selected, json_path, sizeof(json_path));
        book_path(csv_name, csv_final, sizeof(csv_final));
        snprintf(csv_tmp, sizeof(csv_tmp), "%s/%s.tmp", VOCAB_BOOK_DIR, csv_name);
        open_set(VOCAB_OPEN_IMPORTING, "Importing JSON to CSV");
        unlink(csv_tmp);
        if (!json_to_csv(json_path, csv_tmp, &count_hint, &s_open)) { unlink(csv_tmp); open_set(VOCAB_OPEN_ERROR, "JSON format unsupported"); vTaskDelete(NULL); return; }
        unlink(csv_final);
        if (rename(csv_tmp, csv_final) != 0) { unlink(csv_tmp); open_set(VOCAB_OPEN_ERROR, "Cannot install CSV"); vTaskDelete(NULL); return; }
        memcpy(selected, csv_name, strlen(csv_name) + 1);
    }

    open_set(VOCAB_OPEN_INDEXING, count_hint ? "Preparing wordbook" : "Checking wordbook");
    esp_err_t err = select_csv_book(selected, count_hint, count_hint ? NULL : &s_open);
    if (err != ESP_OK) { open_set(VOCAB_OPEN_ERROR, "Wordbook validation failed"); vTaskDelete(NULL); return; }

    open_set(VOCAB_OPEN_PLANNING, "Building today's session");
    err = vocab_start_session(mode);
    if (err != ESP_OK) {
        if (mode == VOCAB_MODE_LEARN) {
            vocab_stats_t st;
            if (vocab_get_stats(&st) && st.today_done >= st.daily_target) open_set(VOCAB_OPEN_ERROR, "Today's plan is complete");
            else open_set(VOCAB_OPEN_ERROR, "No words available today");
        } else open_set(VOCAB_OPEN_ERROR, "Learn words before dictation");
        vTaskDelete(NULL); return;
    }

    s_open.words_done = s_active_count;
    snprintf((char *)s_open.message, sizeof(s_open.message), "%s ready", selected);
    s_open.state = VOCAB_OPEN_READY;
    vTaskDelete(NULL);
}

esp_err_t vocab_open_book_async(const char *filename, vocab_mode_t mode)
{
    if (!filename || !*filename || strlen(filename) >= VOCAB_FILENAME_MAX || strchr(filename, '/') || strstr(filename, "..")) return ESP_ERR_INVALID_ARG;
    if (s_open.state == VOCAB_OPEN_PREPARING || s_open.state == VOCAB_OPEN_IMPORTING ||
        s_open.state == VOCAB_OPEN_INDEXING || s_open.state == VOCAB_OPEN_PLANNING ||
        s_dl.state == VOCAB_DL_DOWNLOADING || s_dl.state == VOCAB_DL_CONVERTING) return ESP_ERR_INVALID_STATE;
    const sdmon_status_t *sd = sd_monitor_get_status();
    if (!sd || !sd->mounted) return ESP_ERR_INVALID_STATE;
    memset((void *)&s_open, 0, sizeof(s_open));
    s_open.state = VOCAB_OPEN_PREPARING; s_open.mode = mode;
    memcpy((char *)s_open.filename, filename, strlen(filename) + 1);
    snprintf((char *)s_open.message, sizeof(s_open.message), "Opening %s", filename);
    if (xTaskCreate(open_book_task, "vocab_open", 8192, NULL, 4, NULL) != pdPASS) { open_set(VOCAB_OPEN_ERROR, "Cannot start worker"); return ESP_ERR_NO_MEM; }
    return ESP_OK;
}

const vocab_open_status_t *vocab_open_status(void)
{
    return (const vocab_open_status_t *)&s_open;
}

void vocab_open_ack(void)
{
    if (s_open.state == VOCAB_OPEN_READY || s_open.state == VOCAB_OPEN_ERROR) {
        s_open.state = VOCAB_OPEN_IDLE; s_open.message[0] = '\0'; s_open.filename[0] = '\0';
    }
}

int vocab_catalog_count(void)
{
    return (int)(sizeof(CATALOG) / sizeof(CATALOG[0]));
}

bool vocab_catalog_get(int index, vocab_catalog_item_t *out)
{
    if (!out || index < 0 || index >= vocab_catalog_count()) return false;
    *out = CATALOG[index];
    return true;
}

bool vocab_catalog_is_downloaded(int index)
{
    if (index < 0 || index >= vocab_catalog_count()) return false;
    const sdmon_status_t *sd = sd_monitor_get_status();
    if (!sd || !sd->mounted) return false;
    char path[160];
    book_path(CATALOG[index].filename, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 32;
}




static void csv_write_quoted(FILE *f, const char *s)
{
    fputc('"', f);
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
            if (*p == '"') fputc('"', f);
            if (*p == '\r' || *p == '\n') fputc(' ', f);
            else fputc(*p, f);
        }
    }
    fputc('"', f);
}

#define VOCAB_JSON_OBJ_MAX 4096

static bool json_write_word_object(FILE *out, const char *obj_text)
{
    if (!out || !obj_text || !*obj_text) return false;

    cJSON *obj = cJSON_Parse(obj_text);
    if (!obj) return false;

    cJSON *jword = cJSON_GetObjectItemCaseSensitive(obj, "word");
    cJSON *jphon = cJSON_GetObjectItemCaseSensitive(obj, "phonetic");
    cJSON *jtrans = cJSON_GetObjectItemCaseSensitive(obj, "translations");

    if (!cJSON_IsString(jword) || !jword->valuestring || !jword->valuestring[0]) {
        cJSON_Delete(obj);
        return false;
    }

    char meaning[VOCAB_MEANING_MAX];
    meaning[0] = '\0';

    if (cJSON_IsArray(jtrans)) {
        int n = cJSON_GetArraySize(jtrans);
        for (int i = 0; i < n; ++i) {
            cJSON *part = cJSON_GetArrayItem(jtrans, i);
            if (!cJSON_IsString(part) || !part->valuestring || !part->valuestring[0]) continue;
            if (meaning[0]) utf8_append_safe(meaning, sizeof(meaning), "; ");
            utf8_append_safe(meaning, sizeof(meaning), part->valuestring);
            if (strlen(meaning) >= sizeof(meaning) - 4) break;
        }
    } else if (cJSON_IsString(jtrans) && jtrans->valuestring) {
        utf8_copy_safe(meaning, sizeof(meaning), jtrans->valuestring);
    }

    if (!meaning[0]) {
        cJSON_Delete(obj);
        return false;
    }

    const char *phon = (cJSON_IsString(jphon) && jphon->valuestring) ? jphon->valuestring : "";

    csv_write_quoted(out, jword->valuestring); fputc(',', out);
    csv_write_quoted(out, phon);               fputc(',', out);
    csv_write_quoted(out, meaning);            fputc(',', out);
    csv_write_quoted(out, "");                 fputc('\n', out);

    cJSON_Delete(obj);
    return true;
}

static bool json_to_csv(const char *json_path, const char *csv_tmp_path, uint32_t *out_words, volatile vocab_open_status_t *status)
{
    struct stat in_st;
    size_t input_total = (stat(json_path, &in_st) == 0 && in_st.st_size > 0) ? (size_t)in_st.st_size : 0;
    FILE *in = fopen(json_path, "rb");
    if (!in) return false;

    if (status) { status->bytes_done = 0; status->bytes_total = input_total; status->words_done = 0; }
    FILE *out = fopen(csv_tmp_path, "w");
    if (!out) {
        fclose(in);
        return false;
    }

    char in_buf[512];
    char out_buf[512];
    (void)setvbuf(in, in_buf, _IOFBF, sizeof(in_buf));
    (void)setvbuf(out, out_buf, _IOFBF, sizeof(out_buf));

    fprintf(out, "word,phonetic,meaning,example\n");

    // Keep the per-word JSON object on the heap. The downloader task has a
    // deliberately modest stack, so large temporary JSON buffers must not live
    // on that stack.
    char *obj = malloc(VOCAB_JSON_OBJ_MAX);
    if (!obj) {
        fclose(in);
        fclose(out);
        unlink(csv_tmp_path);
        return false;
    }

    // Stream the top-level JSON and capture each object nested directly below
    // it (the vocabulary entries in the "words" array). Each object is parsed
    // independently, so a several-megabyte wordbook never needs to live in RAM.
    size_t obj_len = 0;
    int root_brace_depth = 0;
    int capture_depth = 0;
    bool capturing = false;
    bool capture_overflow = false;
    bool in_string = false;
    bool escaped = false;
    uint32_t count = 0;
    size_t bytes_read = 0;

    int ch;
    while ((ch = fgetc(in)) != EOF) {
        bytes_read++;
        if (status && (bytes_read & 0x0FFFu) == 0) {
            status->bytes_done = bytes_read; status->words_done = count;
            vTaskDelay(1);
        }
        if (capturing) {
            if (obj_len + 1 < VOCAB_JSON_OBJ_MAX) {
                obj[obj_len++] = (char)ch;
            } else {
                capture_overflow = true;
            }
        }

        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == '{') {
            if (!capturing && root_brace_depth == 1) {
                capturing = true;
                capture_overflow = false;
                obj_len = 1;
                obj[0] = '{';
                capture_depth = 1;
            } else if (capturing) {
                capture_depth++;
            }
            root_brace_depth++;
            continue;
        }

        if (ch == '}') {
            if (capturing) {
                capture_depth--;
                if (capture_depth == 0) {
                    if (!capture_overflow && obj_len < VOCAB_JSON_OBJ_MAX) {
                        obj[obj_len] = '\0';
                        if (json_write_word_object(out, obj)) {
                            count++;
                            // A local SD import runs from the UI task. Yield
                            // periodically so multi-thousand-word books do not
                            // monopolize the CPU or trip a task watchdog.
                            if (status) status->words_done = count;
                            if ((count & 0x3Fu) == 0) vTaskDelay(1);
                        }
                    } else {
                        ESP_LOGW(TAG, "Skipping oversized JSON word object");
                    }
                    capturing = false;
                    obj_len = 0;
                }
            }
            if (root_brace_depth > 0) root_brace_depth--;
        }
    }

    bool ok = !ferror(in) && !ferror(out) && count > 0;
    free(obj);
    fclose(in);
    fclose(out);
    if (out_words) *out_words = count;
    if (status) { status->bytes_done = input_total ? input_total : bytes_read; status->bytes_total = input_total; status->words_done = count; }
    return ok;
}

static void dl_set(vocab_download_state_t state, const char *msg)
{
    if (msg) snprintf((char *)s_dl.message, sizeof(s_dl.message), "%.63s", msg);
    s_dl.state = state;
}

static bool download_url_to_file(const char *url, const char *path,
                                 char *why, size_t why_len, bool *fatal)
{
    if (fatal) *fatal = false;
    if (why && why_len) why[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 20000,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .user_agent = "ESP32-VocabLab/1.0",
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        if (fatal) *fatal = true;
        if (why) snprintf(why, why_len, "HTTP init failed");
        return false;
    }

    FILE *out = fopen(path, "wb");
    if (!out) {
        esp_http_client_cleanup(client);
        if (fatal) *fatal = true;
        if (why) snprintf(why, why_len, "Cannot write SD cache");
        return false;
    }

    char io_buf[2048];
    (void)setvbuf(out, io_buf, _IOFBF, sizeof(io_buf));

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        fclose(out);
        unlink(path);
        esp_http_client_cleanup(client);
        if (why) snprintf(why, why_len, "Connect failed");
        return false;
    }

    int64_t len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        fclose(out);
        unlink(path);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (why) snprintf(why, why_len, "HTTP %d", status);
        return false;
    }

    s_dl.bytes_done = 0;
    s_dl.bytes_total = len > 0 ? (size_t)len : 0;

    char *buf = malloc(2048);
    if (!buf) {
        fclose(out);
        unlink(path);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (fatal) *fatal = true;
        if (why) snprintf(why, why_len, "Out of memory");
        return false;
    }

    int n = 0;
    while ((n = esp_http_client_read(client, buf, 2048)) > 0) {
        if (fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
            free(buf);
            fclose(out);
            unlink(path);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            if (fatal) *fatal = true;
            if (why) snprintf(why, why_len, "SD write failed");
            return false;
        }
        s_dl.bytes_done += (size_t)n;
    }

    free(buf);
    bool file_ok = (fflush(out) == 0 && !ferror(out));
    fclose(out);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (n < 0) {
        unlink(path);
        if (why) snprintf(why, why_len, "Network read failed");
        return false;
    }
    if (!file_ok) {
        unlink(path);
        if (fatal) *fatal = true;
        if (why) snprintf(why, why_len, "SD flush failed");
        return false;
    }
    if (s_dl.bytes_done == 0) {
        unlink(path);
        if (why) snprintf(why, why_len, "Empty download");
        return false;
    }
    return true;
}

static void download_task(void *arg)
{
    int index = (int)(intptr_t)arg;
    const vocab_catalog_item_t *item = &CATALOG[index];

    if (!vocab_ensure_dirs()) {
        dl_set(VOCAB_DL_ERROR, "SD card not mounted");
        vTaskDelete(NULL);
        return;
    }

    char json_path[176], csv_tmp[176], final_path[176];
    snprintf(json_path, sizeof(json_path), "%s/%s.json.part", VOCAB_CACHE_DIR, item->id);
    snprintf(csv_tmp, sizeof(csv_tmp), "%s/%s.tmp", VOCAB_BOOK_DIR, item->filename);
    book_path(item->filename, final_path, sizeof(final_path));
    unlink(json_path);
    unlink(csv_tmp);

    // jsDelivr is tried first because GitHub Raw can be slow or unreachable on
    // some networks. The original GitHub Raw URL remains as an automatic fallback.
    char mirror_url[256];
    snprintf(mirror_url, sizeof(mirror_url),
             "https://cdn.jsdelivr.net/gh/grhliu/wordtyper-vocabularies@main/vocabularies/%s.json",
             item->id);

    const char *urls[2] = { mirror_url, item->url };
    const char *names[2] = { "CDN", "GitHub" };
    bool downloaded = false;
    char why[64] = {0};

    for (int attempt = 0; attempt < 2 && !downloaded; ++attempt) {
        bool fatal = false;
        s_dl.bytes_done = 0;
        s_dl.bytes_total = 0;
        snprintf((char *)s_dl.message, sizeof(s_dl.message),
                 "Connecting via %s...", names[attempt]);

        downloaded = download_url_to_file(urls[attempt], json_path,
                                          why, sizeof(why), &fatal);
        if (fatal) {
            dl_set(VOCAB_DL_ERROR, why[0] ? why : "Download failed");
            vTaskDelete(NULL);
            return;
        }
        if (!downloaded) {
            ESP_LOGW(TAG, "%s download failed for %s: %s",
                     names[attempt], item->id, why);
        }
    }

    if (!downloaded) {
        snprintf((char *)s_dl.message, sizeof(s_dl.message),
                 "Sources failed: %.44s", why[0] ? why : "network error");
        s_dl.state = VOCAB_DL_ERROR;
        vTaskDelete(NULL);
        return;
    }

    dl_set(VOCAB_DL_CONVERTING, "Installing wordbook...");
    uint32_t words = 0;
    if (!json_to_csv(json_path, csv_tmp, &words, NULL)) {
        unlink(json_path);
        unlink(csv_tmp);
        dl_set(VOCAB_DL_ERROR, "Downloaded JSON format unsupported");
        vTaskDelete(NULL);
        return;
    }

    unlink(json_path);
    unlink(final_path);
    if (rename(csv_tmp, final_path) != 0) {
        unlink(csv_tmp);
        dl_set(VOCAB_DL_ERROR, "Cannot install wordbook");
        vTaskDelete(NULL);
        return;
    }

    // Make the newly installed book active immediately. Even if selecting it
    // fails, the CSV itself remains successfully installed on the SD card.
    esp_err_t select_err = vocab_select_book(item->filename);
    if (select_err == ESP_OK) {
        snprintf((char *)s_dl.message, sizeof(s_dl.message),
                 "Saved %s | %lu words | ACTIVE",
                 item->filename, (unsigned long)words);
    } else {
        snprintf((char *)s_dl.message, sizeof(s_dl.message),
                 "Saved %s | %lu words",
                 item->filename, (unsigned long)words);
    }

    s_dl.state = VOCAB_DL_OK;
    ESP_LOGI(TAG, "downloaded %s: %lu words", item->id, (unsigned long)words);
    vTaskDelete(NULL);
}

esp_err_t vocab_download_catalog_async(int index)
{
    if (index < 0 || index >= vocab_catalog_count()) return ESP_ERR_INVALID_ARG;
    if (s_dl.state == VOCAB_DL_DOWNLOADING || s_dl.state == VOCAB_DL_CONVERTING ||
        s_open.state == VOCAB_OPEN_PREPARING || s_open.state == VOCAB_OPEN_IMPORTING ||
        s_open.state == VOCAB_OPEN_INDEXING || s_open.state == VOCAB_OPEN_PLANNING) return ESP_ERR_INVALID_STATE;
    const sdmon_status_t *sd = sd_monitor_get_status();
    if (!sd || !sd->mounted) return ESP_ERR_INVALID_STATE;
    s_dl.state = VOCAB_DL_DOWNLOADING;
    s_dl.bytes_done = 0;
    s_dl.bytes_total = 0;
    s_dl.catalog_index = index;
    snprintf((char *)s_dl.message, sizeof(s_dl.message), "Downloading %s", CATALOG[index].title);
    if (xTaskCreate(download_task, "vocab_dl", 8192, (void *)(intptr_t)index, 4, NULL) != pdPASS) {
        dl_set(VOCAB_DL_ERROR, "Cannot start downloader");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

const vocab_download_status_t *vocab_download_status(void)
{
    return (const vocab_download_status_t *)&s_dl;
}
