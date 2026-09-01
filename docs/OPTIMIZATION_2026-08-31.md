# 2026-08-31 build and performance fixes

## Build fix
- Fixed `vocabulary.c` filename copy that triggered GCC `-Werror=format-truncation`.
- The copy is explicitly bounded and always NUL-terminated; compiler warnings are not suppressed.

## INVENTORY
- Increased inventory capacity from 100 to 256 records.
- Removed the previous hard 24-row rendering limit.
- Added an 8-row virtualized LVGL pool: the list can scroll through all matching records while only a small fixed number of row widgets exists.
- Filter changes, row selection, quantity edits and history scrolling reuse widgets instead of rebuilding dozens of objects.
- Search matches NAME / SPEC / LCSC.
- History retains and displays the newest cached entries when the history file grows large.
- CSV parsing is now single-pass rather than repeatedly moving the remainder of each quoted line.
- Inventory load/save uses buffered stdio to reduce FATFS small I/O overhead.

## Overall responsiveness
- Fixed the global tap debounce: normal UI operations are no longer throttled to one accepted tap every 350 ms.
- Uses a 180 ms screen-transition guard plus an 80 ms normal tap debounce.
- Periodic labels skip `lv_label_set_text()` when their contents have not changed.
- LVGL draw buffer increased from 32 to 40 scanlines and explicitly placed in DMA-capable RAM.
- ILI9341 SPI clock increased from 20 MHz to 40 MHz.
- Each LVGL flush sends its pixel payload as one large DMA SPI transaction instead of many 512-byte transactions.
- Removed redundant full-screen draw commands at boot and after touch recalibration.

## Validation performed here
- `inventory.c`: host GCC syntax/warning check with `-O2 -Wall -Wextra -Werror -Wformat-truncation=2`.
- Original vocabulary filename-copy warning pattern: dedicated regression compile with `-Wformat-truncation=2`.
- Repository `tools/test_parse.py`: passed.
- Structural checks on every edited C/H source: passed.

## Final target build / hardware verification
This environment does not contain the ESP-IDF Xtensa toolchain, so run the final target build on your ESP-IDF 5.5.x machine:

```bash
idf.py fullclean
idf.py build
```

After flashing, verify:
1. INVENTORY can scroll past the old 24-item boundary.
2. ALL / IN / OUT / HIST filters and search remain responsive.
3. Row select, +/- quantity and delete actions persist correctly to SD.
4. LCD rendering is stable at the 40 MHz SPI setting.

## BOOKS download feedback
- BOOKS now shows a large two-line status panel instead of a narrow one-line hint.
- Tapping a download gives immediate feedback.
- Active download shows book title plus percent/KB progress.
- Conversion/install state is shown explicitly.
- Success shows `DONE` and installed word count; failure shows `FAILED` plus the error.
- Online catalog rows clearly show `[ON SD]` or `[DOWNLOAD]`.
