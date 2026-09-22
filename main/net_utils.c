#include "net_utils.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "net";

static bool s_time_synced;
static net_time_src_t s_time_src = NET_TIME_SRC_NONE;
static net_loc_t s_loc;
static bool s_loc_dirty;
static net_loc_state_t s_loc_state = NET_LOC_IDLE;

// Guards TZ changes (setenv/tzset) against concurrent localtime_r() calls.
static SemaphoreHandle_t s_tz_mutex;

// POSIX TZ syntax uses a reversed sign: "CST-8" means UTC+8. The default is
// China Standard Time (UTC+8); net_tz_apply() overrides it dynamically with
// an "UTC<sign>H[:MM]" string for any whole-minute offset.
#define NET_DEFAULT_TZ_OFFSET_MIN 480
#define NET_NVS_NAMESPACE "storage"
#define NET_NVS_KEY_TZ    "tz_off_min"

static int s_tz_offset_min = NET_DEFAULT_TZ_OFFSET_MIN;

static int load_tz_offset_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return NET_DEFAULT_TZ_OFFSET_MIN;
    }
    int32_t off = 0;
    esp_err_t err = nvs_get_i32(h, NET_NVS_KEY_TZ, &off);
    nvs_close(h);
    if (err != ESP_OK || off < -14 * 60 || off > 14 * 60) {
        return NET_DEFAULT_TZ_OFFSET_MIN;
    }
    return (int)off;
}

// Build the POSIX TZ string for a whole-minute offset east of UTC
// (sign reversed: UTC+8 -> "UTC-8", UTC+5:30 -> "UTC-5:30", UTC0 -> "UTC0").
static void tz_string_for(char *buf, size_t len, int offset_min)
{
    int a = abs(offset_min);
    if (a == 0) {
        snprintf(buf, len, "UTC0");
        return;
    }
    char sgn = offset_min > 0 ? '-' : '+';
    int h = a / 60, m = a % 60;
    if (m) snprintf(buf, len, "UTC%c%d:%02d", sgn, h, m);
    else   snprintf(buf, len, "UTC%c%d", sgn, h);
}

static void tz_apply(int offset_min)
{
    if (s_tz_mutex) xSemaphoreTake(s_tz_mutex, portMAX_DELAY);
    s_tz_offset_min = offset_min;
    char tz[32];
    tz_string_for(tz, sizeof(tz), offset_min);
    setenv("TZ", tz, 1);
    tzset();
    if (s_tz_mutex) xSemaphoreGive(s_tz_mutex);
    ESP_LOGI(TAG, "timezone: UTC%s%d:%02d (offset %d min)",
             offset_min > 0 ? "+" : "-",
             abs(offset_min) / 60, abs(offset_min) % 60, offset_min);
}

void net_time_init(void)
{
    s_tz_mutex = xSemaphoreCreateMutex();
    s_tz_offset_min = load_tz_offset_from_nvs();
    tz_apply(s_tz_offset_min);
}

int net_tz_offset_minutes(void)
{
    return s_tz_offset_min;
}

esp_err_t net_tz_apply(int offset_minutes)
{
    if (offset_minutes < -14 * 60 || offset_minutes > 14 * 60) {
        return ESP_ERR_INVALID_ARG;
    }
    if (offset_minutes == s_tz_offset_min) return ESP_OK; // no-op, skip flash write
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NET_NVS_KEY_TZ, (int32_t)offset_minutes);
        nvs_commit(h);
        nvs_close(h);
    }
    tz_apply(offset_minutes);
    return ESP_OK;
}

// ---------------- local time accessors ----------------
static bool lock_local_time(void)
{
    if (!s_tz_mutex) return false;
    return xSemaphoreTake(s_tz_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

static void unlock_local_time(void)
{
    if (s_tz_mutex) xSemaphoreGive(s_tz_mutex);
}

// ---------------- SNTP ----------------
static void sntp_time_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    s_time_src = NET_TIME_SRC_WIFI;
    ESP_LOGI(TAG, "SNTP time synchronized");
}

void net_sntp_start(void)
{
    if (s_time_synced) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "ntp.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(sntp_time_cb);
    esp_sntp_init();
}

bool net_time_synced(void)
{
    return s_time_synced;
}

net_time_src_t net_time_source(void)
{
    return s_time_src;
}

void net_time_from_usb(time_t unix_time)
{
    // Reject obviously invalid epochs (out-of-range or long before 2020).
    if (unix_time < 1577836800LL || unix_time > 4102444800LL) {
        ESP_LOGW(TAG, "rejecting out-of-range PC time %lld", (long long)unix_time);
        return;
    }
    struct timeval tv = { .tv_sec = unix_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_time_synced = true;
    s_time_src = NET_TIME_SRC_USB;
}

bool net_time_str(char *buf, size_t len)
{
    if (!buf || len == 0) return false;
    if (!s_time_synced) {
        snprintf(buf, len, "1970-01-01 00:00:00");
        return false;
    }
    time_t now = 0;
    time(&now);
    struct tm tmv;
    bool locked = lock_local_time();
    localtime_r(&now, &tmv);
    if (locked) unlock_local_time();
    if (tmv.tm_year < 120) {
        snprintf(buf, len, "1970-01-01 00:00:00");
        return false;
    }
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return true;
}

// ---------------- Geolocation ----------------
// Plain-HTTP endpoints only: HTTPS (api.ip.sb / ipapi.co) fails TLS handshake
// in some LAN/firewall environments and just wastes time.
static const char *LOC_URLS[] = {
    "http://ip-api.com/json/?fields=status,country,regionName,city,lat,lon",
    "http://ipwho.is/",
};

// Generic parser: tries multiple field names across different providers.
static void parse_loc_generic(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "loc: invalid JSON");
        return;
    }
    // Some providers wrap in a "data" object or have a "status"/"success" flag.
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (status && cJSON_IsString(status) && strcmp(status->valuestring, "success") != 0) {
        cJSON_Delete(root);
        return;
    }
    const cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
    if (success && !cJSON_IsTrue(success)) {
        cJSON_Delete(root);
        return;
    }

    net_loc_t tmp = {0};
    const cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(root, "city");
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(tmp.city, sizeof(tmp.city), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "regionName");
    if (!cJSON_IsString(item)) item = cJSON_GetObjectItemCaseSensitive(root, "region");
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(tmp.region, sizeof(tmp.region), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "country");
    if (!cJSON_IsString(item)) item = cJSON_GetObjectItemCaseSensitive(root, "country_name");
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(tmp.country, sizeof(tmp.country), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "lat");
    if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItemCaseSensitive(root, "latitude");
    if (cJSON_IsNumber(item)) {
        snprintf(tmp.lat, sizeof(tmp.lat), "%.4f", item->valuedouble);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "lon");
    if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItemCaseSensitive(root, "longitude");
    if (cJSON_IsNumber(item)) {
        snprintf(tmp.lon, sizeof(tmp.lon), "%.4f", item->valuedouble);
    }

    if (tmp.city[0] || tmp.country[0]) tmp.ok = true;
    s_loc = tmp;
    s_loc_dirty = true;
    cJSON_Delete(root);
}

static void loc_task(void *arg)
{
    for (unsigned i = 0; i < sizeof(LOC_URLS) / sizeof(LOC_URLS[0]); ++i) {
        esp_http_client_config_t cfg = {
            .url = LOC_URLS[i],
            .timeout_ms = 8000,
            .buffer_size = 1024,
            .skip_cert_common_name_check = true, // no CA bundle; fine for geo lookup
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) continue;

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            if (status == 200) {
                char buf[1024];
                int len = esp_http_client_read_response(client, buf, sizeof(buf) - 1);
                if (len > 0) {
                    buf[len] = '\0';
                    parse_loc_generic(buf);
                    if (s_loc.ok) {
                        esp_http_client_cleanup(client);
                        s_loc_state = NET_LOC_OK;
                        vTaskDelete(NULL);
                        return;
                    }
                }
            } else {
                ESP_LOGW(TAG, "loc[%u]: http status %d", i, status);
            }
        } else {
            ESP_LOGW(TAG, "loc[%u]: perform failed %s", i, esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }
    s_loc_state = NET_LOC_FAILED;
    ESP_LOGW(TAG, "loc: all endpoints failed");
    vTaskDelete(NULL);
}

esp_err_t net_loc_start_query(void)
{
    if (s_loc_state == NET_LOC_QUERYING) return ESP_OK;
    s_loc_state = NET_LOC_QUERYING;
    if (xTaskCreate(loc_task, "net_loc", 6144, NULL, 5, NULL) != pdPASS) {
        s_loc_state = NET_LOC_FAILED;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

const net_loc_t *net_loc_get(void)
{
    return &s_loc;
}

net_loc_state_t net_loc_get_state(void)
{
    return s_loc_state;
}
