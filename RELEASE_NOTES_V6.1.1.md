# V6.1.1 Memory / Build Fix

This release is based on V6.1 and fixes the actual ESP-IDF 5.5.4 / GCC 14.2.0
build log supplied from the target project.

## Fixed

### DRAM linker overflow

The DICTATION handwriting canvas previously used a 216 x 82 16-bit true-color
static framebuffer:

- old static framebuffer: 35,424 bytes
- new framebuffer: LVGL `LV_IMG_CF_INDEXED_1BIT`
- new framebuffer: about 2.2 KB including palette
- static DRAM reduction: about 33 KB

The handwriting pad only needs two colors (background + stroke), so this change
does not remove the handwriting feature.

### GCC warnings

Removed two unused static helpers from `main/vocabulary.c`:

- `hex4()`
- `utf8_put()`

No warning-suppression compiler flags were added; the unused code itself was
removed.

## Build

Recommended:

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

Target environment:

- ESP-IDF 5.5.4
- GCC 14.2.0
- ESP32
- LVGL ~8.4.0
