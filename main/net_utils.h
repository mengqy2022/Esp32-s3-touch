#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char city[64];        // e.g. "Shenzhen"
    char region[64];      // e.g. "Guangdong"
    char country[64];     // e.g. "China"
    char lat[24];         // latitude as string
    char lon[24];         // longitude as string
    bool ok;
} net_loc_t;

typedef enum {
    NET_LOC_IDLE = 0,     // not queried yet
    NET_LOC_QUERYING,     // request in flight
    NET_LOC_OK,           // got a valid location
    NET_LOC_FAILED,       // request failed
} net_loc_state_t;

// Configure the device local timezone. The project defaults to China Standard
// Time (UTC+8); change NET_DEFAULT_TZ in net_utils.c if the device is deployed
// elsewhere. Call once during boot before vocabulary date logic starts.
void net_time_init(void);
// Start SNTP time sync (call after WiFi is connected).
void net_sntp_start(void);
// Returns true once system time has been synchronized from an NTP server.
bool net_time_synced(void);
// Format current local time into buf ("%Y-%m-%d %H:%M:%S"). Returns false if not synced.
bool net_time_str(char *buf, size_t len);

// Query geolocation (city/coords) in a background task. Uses ip-api.com.
// Result is stored internally; poll with net_loc_get() / net_loc_get_state().
esp_err_t net_loc_start_query(void);
// Returns a pointer to the last location result (valid until next query).
const net_loc_t *net_loc_get(void);
// Returns the current query state (idle/querying/ok/failed).
net_loc_state_t net_loc_get_state(void);

#ifdef __cplusplus
}
#endif
