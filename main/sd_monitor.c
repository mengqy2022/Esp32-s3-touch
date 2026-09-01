#include "sd_monitor.h"
#include "board_pins.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define MOUNT_POINT "/sdcard"
static const char *TAG = "sdmon";
static sdmmc_card_t *s_card;
static sdmon_status_t s_status;
static bool s_bus_ready;

static void clear_runtime_fields(void)
{
    s_status.file_count = 0;
    s_status.last_read_bytes = 0;
    s_status.sample_file[0] = '\0';
}

esp_err_t sd_monitor_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = SDMON_IDLE;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_bus_ready = true;
        return ESP_OK;
    }
    s_status.state = SDMON_MOUNT_ERROR;
    s_status.last_err = err;
    return err;
}

void sd_monitor_unmount(void)
{
    if (s_status.mounted && s_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    }
    s_card = NULL;
    s_status.mounted = false;
    s_status.capacity_bytes = 0;
}

static bool is_regular_file(const char *dir_path, const struct dirent *ent, char *path, size_t path_len)
{
    snprintf(path, path_len, "%s/%s", dir_path, ent->d_name);
#ifdef DT_REG
    if (ent->d_type == DT_REG) return true;
    if (ent->d_type != DT_UNKNOWN) return false;
#endif
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

esp_err_t sd_monitor_ensure_mounted(void)
{
    if (s_status.mounted && s_card) return ESP_OK;
    if (!s_bus_ready) {
        s_status.state = SDMON_MOUNT_ERROR;
        s_status.last_err = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 12,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = 4000; // conservative: improves compatibility with long SPI wires and cheap adapters

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_CS;
    slot_config.host_id = SPI3_HOST;

    esp_err_t err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        s_status.state = SDMON_MOUNT_ERROR;
        s_status.last_err = err;
        s_status.mounted = false;
        s_status.capacity_bytes = 0;
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
        return err;
    }

    s_status.mounted = true;
    s_status.capacity_bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
    s_status.last_err = ESP_OK;
    return ESP_OK;
}

void sd_monitor_probe(void)
{
    clear_runtime_fields();
    s_status.last_err = ESP_OK;

    // Keep a healthy mount alive. Repeated unmount/remount cycles caused visible
    // UI stalls and could invalidate files opened by inventory/vocabulary tasks.
    esp_err_t err = sd_monitor_ensure_mounted();
    if (err != ESP_OK) return;

    DIR *dir = NULL;
    for (int retry = 0; retry < 3 && !dir; retry++) {
        dir = opendir(MOUNT_POINT);
        if (!dir) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    if (!dir) {
        // The FAT layer can temporarily lose the root directory after card
        // insertion/removal or SPI contention. Force a clean remount next time.
        s_status.state = SDMON_READ_ERROR;
        s_status.last_err = ESP_FAIL;
        ESP_LOGE(TAG, "root directory open failed after retry");
        sd_monitor_unmount();
        return;
    }

    bool got_payload = false;
    bool root_read_ok = false;
    struct dirent *ent;
    char path[160];
    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        root_read_ok = true;
        if (s_status.file_count < 9999) s_status.file_count++;

        if (!got_payload && is_regular_file(MOUNT_POINT, ent, path, sizeof(path))) {
            FILE *f = fopen(path, "rb");
            if (f) {
                uint8_t buf[512];
                size_t n = fread(buf, 1, sizeof(buf), f);
                if (ferror(f)) {
                    fclose(f);
                    continue;
                }
                fclose(f);
                if (n > 0) {
                    got_payload = true;
                    s_status.last_read_bytes = n;
                    strncpy(s_status.sample_file, ent->d_name, sizeof(s_status.sample_file) - 1);
                    s_status.sample_file[sizeof(s_status.sample_file) - 1] = '\0';
                }
            }
        }
    }
    closedir(dir);

    if (got_payload) {
        s_status.state = SDMON_READ_OK;
        s_status.read_count++;
        ESP_LOGI(TAG, "SD payload read OK: %u bytes from %s", (unsigned)s_status.last_read_bytes, s_status.sample_file);
    } else {
        (void)root_read_ok;
        s_status.state = SDMON_EMPTY_OK;
        s_status.read_count++;
        snprintf(s_status.sample_file, sizeof(s_status.sample_file), "<root metadata>");
        ESP_LOGI(TAG, "SD mounted and root metadata read OK; no non-empty regular file found");
    }
}

const sdmon_status_t *sd_monitor_get_status(void)
{
    return &s_status;
}
