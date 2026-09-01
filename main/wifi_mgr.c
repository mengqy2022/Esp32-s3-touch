#include "wifi_mgr.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifimgr";
static const char *NVS_NS = "wifi_creds";
static const char *NVS_SSID = "ssid";
static const char *NVS_PASS = "pass";

static bool s_netif_ready;
static wifi_ap_t s_aps[WIFI_MAX_APS];
static int s_ap_count;
static bool s_scan_ready;
static bool s_scan_fetched;
static wifi_state_t s_state;
static char s_ip[16];
static char s_conn_ssid[WIFI_SSID_MAX + 1];

static void set_state(wifi_state_t st)
{
    s_state = st;
}

// Forward decls (used by event_handler)
bool wifi_mgr_saved_creds_exist(void);
esp_err_t wifi_mgr_auto_connect(void);

static void scan_fetch_results(void)
{
    s_ap_count = 0;
    uint16_t n = WIFI_MAX_APS;
    wifi_ap_record_t recs[WIFI_MAX_APS];
    esp_err_t err = esp_wifi_scan_get_ap_records(&n, recs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "get ap records failed: %s", esp_err_to_name(err));
        return;
    }
    s_ap_count = n;
    for (int i = 0; i < n && i < WIFI_MAX_APS; ++i) {
        snprintf(s_aps[i].ssid, sizeof(s_aps[i].ssid), "%s", recs[i].ssid);
        s_aps[i].rssi = recs[i].rssi;
        s_aps[i].authmode = recs[i].authmode;
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                // If saved credentials exist and no manual connect is pending,
                // auto-connect now that the STA interface is up.
                if (s_state != WIFI_STATE_CONNECTING && wifi_mgr_saved_creds_exist()) {
                    wifi_mgr_auto_connect();
                }
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_state == WIFI_STATE_CONNECTING || s_state == WIFI_STATE_CONNECTED) {
                    set_state(WIFI_STATE_FAILED);
                }
                break;
            case WIFI_EVENT_SCAN_DONE:
                s_scan_ready = true;
                set_state(WIFI_STATE_IDLE);
                break;
            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        set_state(WIFI_STATE_CONNECTED);
    }
}

esp_err_t wifi_mgr_init(void)
{
    if (s_netif_ready) return ESP_OK;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Reduce WiFi memory pressure so LVGL/VOCAB LAB can coexist on ESP32.
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    s_netif_ready = true;
    set_state(WIFI_STATE_IDLE);
    ESP_LOGI(TAG, "WiFi STA initialized");
    return ESP_OK;
}

void wifi_mgr_scan_async(void)
{
    s_scan_ready = false;
    s_scan_fetched = false;
    set_state(WIFI_STATE_SCANNING);
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 300 } },
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(err));
        s_scan_ready = true;
        set_state(WIFI_STATE_IDLE);
    }
}

bool wifi_mgr_poll(void)
{
    if (!s_scan_ready) {
        s_scan_fetched = false;
        return false;
    }
    if (s_scan_fetched) return false;
    s_scan_fetched = true;
    scan_fetch_results();
    return true;
}

bool wifi_mgr_scan_ready(void)
{
    return s_scan_ready;
}

int wifi_mgr_get_ap_count(void)
{
    return s_ap_count;
}

bool wifi_mgr_get_ap(int index, wifi_ap_t *out)
{
    if (index < 0 || index >= s_ap_count || !out) return false;
    *out = s_aps[index];
    return true;
}

static esp_err_t creds_save(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, NVS_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_PASS, password ? password : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool wifi_mgr_saved_creds_exist(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, NVS_SSID, NULL, &len);
    nvs_close(h);
    return err == ESP_OK && len > 1;
}

esp_err_t wifi_mgr_auto_connect(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    char ssid[WIFI_SSID_MAX + 1] = {0};
    char pass[WIFI_PASS_MAX + 1] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);
    err = nvs_get_str(h, NVS_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(h, NVS_PASS, pass, &pass_len);
    }
    nvs_close(h);
    if (err != ESP_OK) return err;

    return wifi_mgr_connect(ssid, pass);
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    // Make sure a finished scan's records are picked up first.
    if (s_scan_ready) wifi_mgr_poll();

    // If the STA is currently connecting/connected, esp_wifi_set_config()
    // fails with ESP_ERR_INVALID_STATE ("sta is connecting"). Tear down first.
    if (s_state == WIFI_STATE_CONNECTING || s_state == WIFI_STATE_CONNECTED) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (password && password[0]) {
        strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        // Retry once: the driver may still be releasing the previous state.
        vTaskDelay(pdMS_TO_TICKS(150));
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "set_config failed: %s", esp_err_to_name(err));
            set_state(WIFI_STATE_IDLE);
            return err;
        }
    }

    snprintf(s_conn_ssid, sizeof(s_conn_ssid), "%s", ssid);
    s_ip[0] = '\0';
    set_state(WIFI_STATE_CONNECTING);
    err = esp_wifi_connect();
    if (err == ESP_OK) {
        // Persist credentials for auto-reconnect on next boot.
        esp_err_t serr = creds_save(ssid, password);
        if (serr != ESP_OK) {
            ESP_LOGW(TAG, "creds save failed: %s", esp_err_to_name(serr));
        }
    } else {
        ESP_LOGW(TAG, "connect failed: %s", esp_err_to_name(err));
        set_state(WIFI_STATE_IDLE);
    }
    return err;
}

void wifi_mgr_disconnect(void)
{
    esp_wifi_disconnect();
    set_state(WIFI_STATE_IDLE);
    s_ip[0] = '\0';
    s_conn_ssid[0] = '\0';
}

wifi_state_t wifi_mgr_get_state(void)
{
    return s_state;
}

bool wifi_mgr_is_connected(void)
{
    return s_state == WIFI_STATE_CONNECTED;
}

const char *wifi_mgr_get_ip(void)
{
    return s_ip;
}

const char *wifi_mgr_get_connected_ssid(void)
{
    return s_conn_ssid;
}
