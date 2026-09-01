# Architecture

## 1. Runtime overview

```text
app_main
  ├─ LCD / Touch / SD bus init
  ├─ Wi-Fi manager + vocabulary NVS init
  ├─ LVGL task (core 1)
  │    ├─ display flush -> ILI9341
  │    ├─ touch read -> XPT2046
  │    └─ UI command queue -> ui_process_cmd()
  ├─ SD scan worker (on demand)
  ├─ Wi-Fi watcher
  ├─ serial file-transfer task
  ├─ BOOT long-press calibration task
  └─ optional vocabulary download task
```

LVGL object creation/update is kept on the LVGL task. Cross-task calls (`ui_set_screen`, `ui_update_status`) are converted into commands through `lv_port_post_cmd()`.

## 2. UI responsiveness

### Command queue

`lv_port.c` now allocates 24 commands instead of 8. The queue stays non-blocking so a background task cannot deadlock the GUI; a dropped command is logged.

### Draw buffer

The display buffer is 32 lines × 320 pixels. This reduces the number of flush transactions for large redraws while still keeping RAM use appropriate for ESP32.

### Touch design

- 320×240 is treated as a fixed touch canvas.
- Header text uses a 220 px safe width.
- Back button occupies a separate top-right region.
- Primary actions use ~40–47 px height where practical.
- Scrollable content is used instead of packing many fixed widgets into the same viewport.

## 3. SD lifecycle

The old SD probe unmounted and remounted the card on every scan. That is unsafe once multiple modules rely on persistent files.

New flow:

1. `sd_monitor_ensure_mounted()` mounts only when needed.
2. `sd_monitor_probe()` reuses a healthy mount.
3. It opens `/sdcard`, scans metadata and samples one regular file.
4. If the root directory can no longer be opened, it treats the mount as stale and unmounts it.
5. The next scan performs a clean mount.

This allows inventory, vocabulary and serial file transfer to share the filesystem without periodic invalidation.

## 4. Vocabulary memory model

A wordbook may contain several thousand rows. The firmware does **not** copy the whole CSV into RAM.

For a study session it stores at most 80 entries:

```c
{ word_index, file_offset }
```

When a card is shown, only that CSV row is read and parsed. The active word is cached until moving to the next card.

Progress is fixed-record binary data (`.vcp`) to avoid rewriting a large JSON document after every answer.

## 5. File ownership

```text
/sdcard/INVENTORY.CSV           inventory module (UTF-8 PRODUCT_NO,MODEL,QTY)
/sdcard/BOM_IN/                 inventory module
/sdcard/BOM_OUT/                inventory module
/sdcard/STOCK_HIST.CSV          inventory module
/sdcard/vocabulary/books/*.csv  vocabulary module
/sdcard/vocabulary/progress/*   vocabulary module
/sdcard/vocabulary/cache/*      vocabulary downloader
```

The SD mount itself is owned by `sd_monitor` and stays alive across ordinary module operations.

## 6. Network security

Vocabulary downloads use `esp_http_client` with `esp_crt_bundle_attach`. The old `CONFIG_ESP_TLS_INSECURE` / `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY` defaults were removed.

## 7. Extension points

- Add another main module: `ui.h` screen enum + `on_menu_btn()` + `do_draw_all()`.
- Add a downloadable wordbook: append a `vocab_catalog_item_t` in `vocabulary.c`.
- Add another vocabulary exercise: extend `vocab_mode_t`, build a session policy, then add a dedicated UI screen.
- Add audio: introduce an I2S driver/task and avoid audio decode work on the LVGL task.
