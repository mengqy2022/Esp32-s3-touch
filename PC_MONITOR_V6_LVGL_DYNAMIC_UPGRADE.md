# PC MONITOR v6.0 LVGL Dynamic Gauge Edition

## Major upgrade

Moved from static dashboard style to dynamic LVGL instrument panel.

## New UI

- LVGL Arc circular gauges
- Animated gauge values
- Smooth transitions
- Dark glass style dashboard
- Touch page switching

## Pages

### Page 1 Main Dashboard

CPU Gauge:
- Usage %
- Temperature

GPU Gauge:
- Usage %
- Temperature

MEM Gauge:
- Usage %
- Used / Total

### Page 2 Detail

- CPU frequency
- GPU VRAM
- Fan RPM
- SSD temperature

### Page 3 System

- Network RX/TX
- Sensor history

## Graphics

- Arc gauge
- Label animation
- Line chart
- Warning indicator

## Timing

Sensor:
1000ms

LVGL:
30ms UI refresh

## Communication

Extended packet:

STAT|CPU|GPU|CPU_TEMP|GPU_TEMP|MEM|MEM_USED|MEM_TOTAL|RX|TX|CPU_FREQ|VRAM|FAN|SSD

## Hardware target

ESP32-S3 + LVGL
320x240 / 480x320 display
