
<h1 align="center">ESP32 Touch Toolbox</h1>
<p align="center">
  <b>Vocab Lab · PC Monitor · SD Checker · Inventory · Games</b><br/>
  A professional, offline-first touch firmware for ESP32 + 2.8" ILI9341 color LCD
</p>

<p align="center">
  <a href="https://www.espressif.com/en/products/socs/esp32"><img src="https://img.shields.io/badge/ESP--IDF-v5.5.4-3B82F6?logo=espressif&logoColor=white" alt="ESP-IDF v5.5.4"/></a>
  <a href="#"><img src="https://img.shields.io/badge/LVGL-8.4-8E6CE0" alt="LVGL 8.4"/></a>
  <a href="#"><img src="https://img.shields.io/badge/board-ESP32--WROOM--32E-16A34A" alt="ESP32-WROOM-32E"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-22C55E" alt="License: MIT"/></a>
</p>

<!-- Replace <owner>/<repo> in the CI badge above with your GitHub repository. -->

---

🌏 README in
[**中文**](https://github.com/mengqy2022/Esp32-s3-touch/docs/README_CN.md) 

## Overview

**ESP32 Touch Toolbox** is a complete, polished firmware for the classic
ESP32-WROOM-32E + ILI9341 2.8" resistive-touch development stack. It bundles
practical daily tools, study software and entertainment into one device:

- **Vocab Lab** — offline vocabulary learning with wordbooks, daily plans,
  spaced repetition and touchscreen dictation.
- **PC Monitor** — a desktop-style dashboard: big clock, USB time sync, and
  live CPU / GPU / MEM usage and temperatures fed from your PC.
- **Classic tools** — SD card checker, Wi-Fi manager, system info, electronic
  component inventory (BOM in/out), and a serial file-transfer service.

Everything runs offline after setup. No cloud, no accounts, no subscription.

> Vocab Lab takes interaction ideas from common vocabulary apps only —
> no commercial assets, images, courses or wordbooks are bundled.
> See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Highlights

| | |
|---|---|
| 🖥️ **PC Monitor screen** | Large HH:MM:SS clock, date/UTC/time-source line, host link status, CPU/GPU/MEM bars with color-coded thresholds (≥70% amber, ≥90% red) and temperatures. |
| ⏱️ **USB time sync** | Wall clock synced over Type-C (`PC:TIME`, every 15 s) and a persistent timezone override in NVS — no Wi-Fi required for accurate time. |
| 💤 **Idle clock** | The menu falls back to the clock dashboard after 2 minutes without touches; tap anywhere to return. |
| 🛡️ **Calibration guard** | `CALIBRATE` now asks for a confirmation dialog — no more accidental 3-point recalibration. |
| 📖 **Offline-first Vocab Lab** | Wordbooks live on MicroSD and are streamed row-by-row, keeping RAM usage flat even for multi-thousand-word books. |
| 🔄 **Graceful serial sharing** | `FILE:` file transfer and `PC:` telemetry coexist on one console UART with a single reader task. |

## Hardware

Target stack: **ESP32-WROOM-32E · 2.8" ILI9341 SPI LCD (320×240) ·
XPT2046 resistive touch · MicroSD (SPI) · common-anode RGB LED.**

| Peripheral | GPIOs |
|---|---|
| ILI9341 (SCK/MOSI/MISO/CS/DC/BL) | 14 / 13 / 12 / 15 / 2 / 21 |
| XPT2046 (SCK/MOSI/MISO/CS/IRQ) | 25 / 32 / 39 / 33 / 36 |
| MicroSD (SCK/MISO/MOSI/CS) | 18 / 19 / 23 / 5 |
| RGB LED (R/G/B, active-low) | 17 / 22 / 16 |
| BOOT button (long-press recalibrate) | GPIO0 |

Full pin map, wiring notes and a component BOM:
**[docs/HARDWARE.md](docs/HARDWARE.md)** — one file, all hardware facts.

## PC Monitor quick start

The dashboard is rendered by the device; the PC only feeds it.

```
pip install pyserial
python tools/pc_monitor_host.py            # auto-detects the COM port
python tools/pc_monitor_host.py --port COM8
```

- Windows: CPU/MEM via `GetSystemTimes`/`GlobalMemoryStatusEx` (ctypes),
  GPU via NVIDIA NVML, CPU temperature via WMI (best effort).
- Linux: `/proc` for CPU/MEM, `nvidia-smi` for GPU.
- Time + timezone are pushed automatically; the device stores the timezone
  in NVS and survives reboots.

Wire protocol and field semantics: [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Build & flash

ESP-IDF **v5.5.4**, target `esp32`, LVGL 8.4 via the Component Manager.

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor     # Windows: COMx
```

Shortcuts: `./build_flash.sh <port>` (Linux/macOS) or `build_flash.bat <port>`
(Windows). On a fresh checkout the first build fetches `managed_components/`
automatically; `build/`, `sdkconfig` and `managed_components/` stay out of git.

> If you upgrade from a checkout built before the PC Monitor merge, run
> `idf.py fullclean` once so the newly enabled `Montserrat 28` clock font
> takes effect.

## SD card setup

Copy `sdcard_template/` onto a FAT32 MicroSD card:

```text
/sdcard/
├── INVENTORY.CSV           stock: PRODUCT_NO,MODEL,QTY (UTF-8)
├── BOM_IN/  BOM_OUT/       inventory CSV exchange folders
└── vocabulary/
    ├── books/              UTF-8 CSV wordbooks
    ├── progress/           per-book learning state
    └── cache/              download scratch space
```

Then on the device: **VOCAB LAB → BOOKS → starter** — or download a
CET/TOEFL/IELTS book right on the device over Wi-Fi.

## Repository layout

```text
.
├── .github/workflows/build.yml    # ESP-IDF v5.5.4 CI, uploads firmware artifacts
├── main/                          # firmware (ESP-IDF component)
│   ├── main.c                     # boot, tasks, idle-clock policy
│   ├── ui.c / ui.h                # LVGL screens + theme + confirmation dialogs
│   ├── pc_monitor.c / .h          # PC: telemetry protocol + state
│   ├── vocabulary.c / .h          # Vocab Lab engine + downloader
│   ├── inventory.c / .h           # BOM stock manager
│   ├── file_xfer.c / .h           # console-UART FILE: protocol
│   ├── net_utils.c / .h           # SNTP, geolocation, USB time sync, timezone
│   ├── wifi_mgr.c / .h            # scan/connect + credential NVS
│   ├── sd_monitor.c / .h          # SD lifecycle owner
│   ├── lcd_ili9341.c / .h         # bare-metal LCD driver
│   ├── xpt2046.c / .h             # touch driver + 3-point calibration
│   ├── sudoku.c · game2048.c · flappy.c
│   ├── lv_port.c / .h             # LVGL task, command queue, input timestamps
│   └── board_pins.h               # single source of truth for GPIOs
├── tools/
│   ├── pc_monitor_host.py         # PC Monitor host agent (Windows/Linux)
│   ├── inventory_tool.py          # GUI serial inventory/BOM manager
│   ├── vocab_convert.py           # JSON/CSV → device wordbook converter
│   ├── material_to_inventory.py   # LCSC order CSV → INVENTORY.CSV
│   └── smoke_test.py / test_parse.py
├── docs/
│   ├── HARDWARE.md                # pinout, wiring, BOM
│   ├── PROTOCOL.md                # FILE: + PC: serial protocols
│   ├── ARCHITECTURE.md            # tasks, threads, ownership
│   ├── VOCABULARY.md              # wordbooks, plans, SRS, dictation
│   ├── PC_MONITOR_CN.md           # PC Monitor 中文手册
│   └── images/                    # logo + screenshot slots
└── sdcard_template/               # starter SD card content
```

## Documentation

| Doc | What it covers |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Task model, threading rules, UI dispatch, SD ownership |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Pinout tables, wiring notes, component BOM |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Console serial wire protocols (`FILE:` and `PC:`) |
| [docs/VOCABULARY.md](docs/VOCABULARY.md) | Wordbook formats, plans, SRS, dictation |
| [README_CN.md](README_CN.md) | 中文完整说明 |
| [CHANGELOG.md](CHANGELOG.md) | Release history |

## Contributing & license

Contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for the
ground rules (offline-first, RAM-friendly, no commercial assets).

Licensed under the **MIT License** — see [LICENSE](LICENSE). Third-party
components and wordbook sources are attributed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); review it before
commercial distribution.

---

*Original interface concepts: SD checker, Wi-Fi manager, inventory, games.
Designed and tested on ESP32-WROOM-32E, ILI9341 320×240, XPT2046.*