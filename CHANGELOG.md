# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **PC Monitor screen** — big clock + CPU / GPU / MEM dashboard with
  color-coded load bars and temperatures.
- **USB time sync** — wall-clock synchronization over the Type-C serial port
  (`PC:TIME`) plus a persistent timezone override (`PC:TZOFF`, stored in NVS).
- `main/pc_monitor.c/h` — parser and state store for the `PC:` telemetry
  protocol, dispatched from the shared console-UART reader in `file_xfer.c`.
- `tools/pc_monitor_host.py` — host agent for Windows/Linux (ctypes CPU/MEM,
  NVIDIA NVML GPU, WMI CPU temperature, time sync, auto reconnect, live
  passthrough of device logs).
- Idle clock behavior — MENU untouched for 120 s falls back to the PC Monitor
  screen; tapping it returns to the menu.
- Touch-calibration confirmation dialog — the MENU `CALIBRATE` button now
  requires an explicit `CONFIRM`, preventing accidental recalibration.
- Large clock font (`Montserrat 28`) enabled via `sdkconfig.defaults`.

### Changed

- `net_utils` — time-source tracking (WiFi / USB), mutex around TZ changes,
  redundant NVS writes skipped.
- `file_xfer` — RX ring buffer raised to 1 KiB so telemetry keeps flowing
  during file transfers.

### Fixed

- Fixed the U+00B7 middle-dot glyph in the PC Monitor hint line, which the
  bundled Montserrat font does not include (rendered as a box).

## 2026-08 — V6.1.1 memory/build fix

### Changed

- Replaced the DICTATION 216×82 16-bit true-color static canvas with an LVGL
  indexed 1-bit canvas and two-color palette.
- Reduced handwriting canvas static DRAM from ~35.4 KB to ~2.2 KB.
- Removed unused `hex4()` and `utf8_put()` helpers from `vocabulary.c`.
- Removed warnings at source instead of suppressing them.

## 2026-08 — GitHub / UX / Vocab Lab revision

### Added

- `main/vocabulary.c/.h` — Vocab Lab dashboard, SD wordbook browser,
  14 online ECDICT-derived wordbook entries, asynchronous HTTPS download,
  on-device JSON→CSV conversion, per-book binary progress database, daily
  target in NVS, learning-card workflow, touchscreen dictation workflow,
  lightweight spaced-repetition scheduling.
- `tools/vocab_convert.py`, SD starter vocabulary template,
  GitHub Actions ESP-IDF build workflow, architecture/vocabulary docs.

### Changed

- Graphite minimalist UI palette, standardized title/back safe areas,
  larger touch targets, scrollable module lists.
- LVGL draw buffer 32 lines, UI command queue 24 entries, SD SPI 8 MHz.
- SD card stays mounted across scans; vocabulary books are indexed, not
  fully loaded into RAM.

## 2026-08 — Base platform

Initial release of the touch toolbox platform:

- ILI9341 320×240 LCD + XPT2046 resistive touch on ESP32-WROOM-32E.
- SD card checker with capacity/content/read verification.
- Wi-Fi scan/connect with saved credentials and auto reconnect.
- SNTP time + IP-based geolocation.
- 3-point touch calibration persisted to NVS.
- Inventory/BOM stock manager with SD persistence.
- Sudoku, 2048 and Flappy Bird mini-games.
- Console serial file-transfer protocol (`FILE:` commands).
- RGB status LED and BOOT-button long-press recalibration.