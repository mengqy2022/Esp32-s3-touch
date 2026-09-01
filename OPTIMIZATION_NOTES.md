# VOCAB LAB V6 optimization notes

Changes in this build:
- Added a lightweight 2048 game module using small static state.
- Added dictation ink preview label to make handwriting recognition feedback visible.
- Enabled WiFi modem power save to reduce coexistence memory pressure with LVGL.
- Kept handwriting recognition template based to avoid loading ML models into RAM.

ESP32 memory note:
WiFi + LVGL + VOCAB LAB can coexist on ESP32, but only with careful RAM usage. Avoid loading large wordbooks into RAM; stream from SD. If reboot persists, capture `heap_caps_get_free_size()` before/after WiFi start and reduce LVGL cache sizes.
