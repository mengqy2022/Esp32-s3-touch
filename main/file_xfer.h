#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the serial file-transfer service (listens for FILE: commands on the
// console UART and reads/writes files on the SD card). Call after SD mount.
esp_err_t file_xfer_start(void);

#ifdef __cplusplus
}
#endif
