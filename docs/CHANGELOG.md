# Modification Summary

## 2026-08 — V6.1.1 memory/build fix

- Replaced the DICTATION 216x82 16-bit true-color static canvas with an LVGL
  indexed 1-bit canvas and two-color palette.
- Reduced handwriting canvas static DRAM from about 35.4 KB to about 2.2 KB.
- Removed unused `hex4()` and `utf8_put()` helpers from `vocabulary.c`.
- No warning suppression was added; the reported warnings were removed at source.
- Keeps the existing handwriting, SD wordbook, LEARN, DICTATION and daily-plan
  functionality.


## 2026-08 — GitHub / UX / Vocab Lab revision

### Added

- `main/vocabulary.c/.h`
- Vocab Lab dashboard
- SD local wordbook browser
- 14 online ECDICT-derived wordbook entries
- asynchronous HTTPS download to SD
- on-device JSON -> CSV conversion
- per-book binary progress database
- daily target in NVS
- learning-card workflow
- touchscreen dictation workflow
- lightweight spaced-repetition scheduling
- `tools/vocab_convert.py`
- SD starter vocabulary template
- GitHub Actions ESP-IDF build workflow
- `.gitignore`
- architecture / vocabulary documentation

### UI / UX

- replaced the previous bright/tech palette with a graphite minimalist palette
- standardized title/back safe areas
- increased touch target sizes
- kept long module/wordbook lists scrollable
- added screen-pointer clearing to avoid stale LVGL object references after rebuilds
- integrated vocabulary pages into the central draw/status/timer dispatch

### Performance / stability

- LVGL draw buffer: 24 -> 32 lines
- UI command queue: 8 -> 24 entries
- SD card stays mounted across scans
- SD SPI: 4 MHz -> 8 MHz
- vocabulary books are indexed, not fully loaded into RAM
- vocabulary downloads run in a background FreeRTOS task

### Security / repository hygiene

- HTTPS wordbook downloads verify with ESP-IDF CA bundle
- removed insecure TLS defaults
- removed generated `managed_components/`, local `sdkconfig*`, Node dependencies and stale backup source
- removed local backup CSV and machine-specific helper scripts
- removed bundled raw font file from the GitHub package
- sanitized machine-local paths from the embedded CJK glyph source header

### Compatibility

- target remains `esp32`
- recommended ESP-IDF is v5.5.4
- LVGL dependency is pinned to `~8.4.0`
- existing SD, Wi-Fi, system info, Sudoku, inventory and serial-transfer runtime modules remain in the build
