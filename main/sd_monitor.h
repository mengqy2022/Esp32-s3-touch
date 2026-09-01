#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDMON_IDLE = 0,
    SDMON_READ_OK,
    SDMON_EMPTY_OK,
    SDMON_MOUNT_ERROR,
    SDMON_READ_ERROR,
} sdmon_state_t;

typedef struct {
    sdmon_state_t state;
    esp_err_t last_err;
    bool mounted;
    uint32_t read_count;
    size_t file_count;
    size_t last_read_bytes;
    uint64_t capacity_bytes;
    char sample_file[48];
} sdmon_status_t;

esp_err_t sd_monitor_init(void);
void sd_monitor_probe(void);
// Mount the SD card if needed and keep it mounted for other modules.
esp_err_t sd_monitor_ensure_mounted(void);
const sdmon_status_t *sd_monitor_get_status(void);
void sd_monitor_unmount(void);

#ifdef __cplusplus
}
#endif
