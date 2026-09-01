#include "net_utils.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

static bool s_time_synced;
static net_loc_t s_loc;
static bool s_loc_dirty;
static net_loc_state_t s_loc_state = NET_LOC_IDLE;

// POSIX TZ syntax uses a reversed sign: "CST-8" means UTC+8. Keeping the
// timezone explicit makes the daily study boundary follow the user's local day
// instead of UTC midnight.
#define NET_DEFAULT_TZ "CST-8"

void net_time_init(void)
{
    setenv("TZ", NET_DEFAULT_TZ, 1);
    tzset();
}

// ---------------- SNTP ----------------
static void sntp_time_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
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
    localtime_r(&now, &tmv);
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
