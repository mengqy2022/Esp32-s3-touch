# PC Monitor v6.1 Build Fix

Fixed:
- LVGL missing lv_font_montserrat_12 compile error
- Font compatibility handling
- Warning cleanup preparation

v6.1 integration:

UI:
- LVGL Arc Gauge ready
- CPU/GPU/MEM dashboard
- Temperature alarm states
- Chart data buffer

Timing:
- Sensor update: 1000ms
- UI refresh: 30ms

Build target:
ESP32-S3 + ESP-IDF 5.5.4 + LVGL

Recommended build:
idf.py fullclean
idf.py build
idf.py -p COMx flash
