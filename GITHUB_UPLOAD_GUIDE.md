# MQY ESP32 Touch Vocabulary Toolbox

## GitHub Upload Project

This package is prepared for direct GitHub repository upload.

## Build

Requires:
- ESP-IDF 5.x
- ESP32 toolchain

Commands:

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

## Directory Notes

- `main/` - firmware source code
- `sdcard_template/` - SD card initial files
- `.github/workflows/` - CI build workflow
