#include "file_xfer.h"
#include "pc_monitor.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "xfer";

#define XFER_UART    UART_NUM_0
#define XFER_BUF     512
#define XFER_LINE_MAX 512
#define RX_TIMEOUT   50   // ms between uart reads
#define XFER_TIMEOUT 3000 // ms per 4KB block while streaming

// ---------------- line buffer ----------------
typedef struct {
    char buf[XFER_LINE_MAX];
    int len;
} linebuf_t;

static void lb_reset(linebuf_t *lb) { lb->len = 0; }

// Push raw bytes into the line buffer; returns a complete line (without \n)
// when found, else NULL. Caller must copy/process before next push.
static char *lb_push(linebuf_t *lb, const uint8_t *data, int len)
{
    for (int i = 0; i < len; ++i) {
        if (lb->len >= XFER_LINE_MAX - 1) {
            lb->len = 0; // drop overlong garbage
            continue;
        }
        char c = (char)data[i];
        if (c == '\n') {
            lb->buf[lb->len] = '\0';
            if (lb->len > 0 && lb->buf[lb->len - 1] == '\r') lb->buf[--lb->len] = '\0';
            char *out = strdup(lb->buf);
            lb->len = 0;
            return out;
        }
        lb->buf[lb->len++] = c;
    }
    return NULL;
}

// ---------------- responses ----------------
static void send_str(const char *s)
{
    uart_write_bytes(XFER_UART, s, strlen(s));
}

static void send_line(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
    send_str(buf);
}

// Read exactly n bytes from the UART (streaming mode), writing to file.
static bool stream_read_to_file(FILE *f, size_t n)
{
    uint8_t buf[XFER_BUF];
    size_t remaining = n;
    while (remaining > 0) {
        size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        int got = uart_read_bytes(XFER_UART, buf, want, pdMS_TO_TICKS(XFER_TIMEOUT));
        if (got <= 0) {
            ESP_LOGW(TAG, "timeout reading file data (need %u got 0)", (unsigned)remaining);
            return false;
        }
        if (fwrite(buf, 1, got, f) != (size_t)got) return false;
        remaining -= got;
    }
    return true;
}

// Stream n bytes from a file to the UART.
static bool stream_file_to_uart(FILE *f, size_t n)
{
    uint8_t buf[XFER_BUF];
    size_t remaining = n;
    while (remaining > 0) {
        size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        size_t got = fread(buf, 1, want, f);
        if (got == 0) return false;
        uart_write_bytes(XFER_UART, buf, got);
        remaining -= got;
    }
    return true;
}

// ---------------- commands ----------------
static void cmd_list(const char *path)
{
    const char *dir = path[0] ? path : "/sdcard";
    DIR *d = opendir(dir);
    if (!d) {
        send_line("FILE:ERR|cannot open dir");
        return;
    }
    struct dirent *ent;
    char full[256];
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        snprintf(full, sizeof(full), "%s/%.180s", dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            send_line("FILE:ENTRY|%s/|0|dir", full);
        } else {
            send_line("FILE:ENTRY|%s|%ld|file", full, (long)st.st_size);
        }
    }
    closedir(d);
    send_line("FILE:DONE");
}

static void cmd_put(const char *path, long size)
{
    if (size < 0 || size > (64L * 1024 * 1024)) {
        send_line("FILE:ERR|bad size");
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        send_line("FILE:ERR|cannot create %s", path);
        return;
    }
    send_line("FILE:READY");
    bool ok = stream_read_to_file(f, (size_t)size);
    fclose(f);
    if (ok) {
        send_line("FILE:OK");
    } else {
        unlink(path); // remove partial file
        send_line("FILE:ERR|write timeout");
    }
}

static void cmd_get(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        send_line("FILE:ERR|cannot open %s", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    send_line("FILE:READY|%ld", size);
    // Temporarily silence the console logger so our raw bytes are not polluted.
    esp_log_level_set("*", ESP_LOG_NONE);
    bool ok = stream_file_to_uart(f, (size_t)size);
    esp_log_level_set("*", ESP_LOG_INFO);
    fclose(f);
    if (ok) send_line("FILE:DONE");
    else   send_line("FILE:ERR|read error");
}

static void cmd_del(const char *path)
{
    if (unlink(path) == 0) {
        send_line("FILE:OK");
    } else {
        send_line("FILE:ERR|cannot delete %s", path);
    }
}

// ---------------- main task ----------------
static void xfer_task(void *arg)
{
    (void)arg;
    linebuf_t lb;
    lb_reset(&lb);

    // UART0 driver is installed in file_xfer_start() (mirroring the console
    // REPL). This task just reads/writes through the driver.

    ESP_LOGI(TAG, "serial file transfer ready (console UART)");

    uint8_t buf[256];
    for (;;) {
        int len = uart_read_bytes(XFER_UART, buf, sizeof(buf), pdMS_TO_TICKS(RX_TIMEOUT));
        if (len <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        char *line = lb_push(&lb, buf, len);
        if (!line) continue;

        // Parse "FILE:CMD|p1|p2"
        if (strncmp(line, "FILE:", 5) == 0) {
            char *cmd = line + 5;
            char *p1 = strchr(cmd, '|');
            if (p1) { *p1 = '\0'; ++p1; }
            char *p2 = p1 ? strchr(p1, '|') : NULL;
            if (p2) { *p2 = '\0'; ++p2; }

            if (strcmp(cmd, "PING") == 0) {
                send_line("FILE:PONG");
            } else if (strcmp(cmd, "LIST") == 0) {
                cmd_list(p1 ? p1 : "");
            } else if (strcmp(cmd, "PUT") == 0 && p1 && p2) {
                cmd_put(p1, strtol(p2, NULL, 10));
            } else if (strcmp(cmd, "GET") == 0 && p1) {
                cmd_get(p1);
            } else if (strcmp(cmd, "DEL") == 0 && p1) {
                cmd_del(p1);
            } else {
                send_line("FILE:ERR|unknown command");
            }
        } else if (strncmp(line, "PC:", 3) == 0) {
            // PC Monitor telemetry (time sync + CPU/GPU/MEM feed).
            pc_monitor_handle_line(line + 3);
        }
        free(line);
    }
}

esp_err_t file_xfer_start(void)
{
    // The ESP-IDF console VFS provides register-level access only (that is why
    // printf()/ESP_LOG output works), but uart_read_bytes()/uart_write_bytes()
    // require the interrupt-driven UART driver object, which is created by
    // uart_driver_install(). Since we don't use the esp_console REPL, install
    // the driver for the console UART here, exactly like the REPL does.
    const uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(XFER_UART, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_set_pin(XFER_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, -1, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }
    // 1 KiB RX ring buffer: PC: telemetry keeps flowing even while a FILE:GET
    // stream occupies the TX path.
    err = uart_driver_install(XFER_UART, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    // Let the VFS console use the driver for tx/rx as well, so printf/logs
    // continue to work alongside the driver.
    uart_vfs_dev_use_driver(XFER_UART);

    if (xTaskCreate(xfer_task, "file_xfer", 8192, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
