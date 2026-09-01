#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_APS    8
#define WIFI_SSID_MAX   32
#define WIFI_PASS_MAX   64

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    int8_t rssi;
    uint8_t authmode; // WIFI_AUTH_*
} wifi_ap_t;

typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;

esp_err_t wifi_mgr_init(void);
void wifi_mgr_scan_async(void);
// Poll: harvest scan results once ready. Returns true when new results were fetched.
bool wifi_mgr_poll(void);
bool wifi_mgr_scan_ready(void);
int wifi_mgr_get_ap_count(void);
bool wifi_mgr_get_ap(int index, wifi_ap_t *out);
wifi_state_t wifi_mgr_get_state(void);
bool wifi_mgr_is_connected(void);

// Connect and (on success) persist the credentials for auto-reconnect.
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);
void wifi_mgr_disconnect(void);

// Credentials persistence (NVS).
// Returns true if saved credentials exist.
bool wifi_mgr_saved_creds_exist(void);
// Try to auto-connect using saved credentials. Returns ESP_OK if a connect was started.
esp_err_t wifi_mgr_auto_connect(void);

const char *wifi_mgr_get_ip(void);
const char *wifi_mgr_get_connected_ssid(void);

#ifdef __cplusplus
}
#endif
