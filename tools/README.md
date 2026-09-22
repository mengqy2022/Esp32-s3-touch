# PC tools

## PC Monitor host agent

`pc_monitor_host.py` pushes wall-clock time + CPU/GPU/MEM telemetry to the
device over the Type-C serial port. The device shows it on its PC MONITOR
screen (big clock + load bars + temperatures) and syncs its clock.

```bash
pip install pyserial
python tools/pc_monitor_host.py                 # auto-detect serial port
python tools/pc_monitor_host.py --port COM8
python tools/pc_monitor_host.py --list-ports
```

Windows uses ctypes (GetSystemTimes / GlobalMemoryStatusEx) and NVIDIA NVML,
so no extra Python dependencies are needed. See `docs/PC_MONITOR_CN.md`.

## Inventory serial tool

`inventory_tool.py` manages the existing inventory/BOM workflow over the ESP32 USB serial connection. It can list, upload, download and delete SD files and merge supported inventory CSV formats. Device output is always compact UTF-8 `PRODUCT_NO,MODEL,QTY`.

Typical dependency:

```bash
pip install pyserial
python tools/inventory_tool.py
```

## Vocabulary converter

`vocab_convert.py` converts either:

- WordTyper/ECDICT-derived JSON with a `words` array; or
- a CSV containing compatible word / phonetic / meaning / example columns

into the exact CSV layout used by Vocab Lab.

```bash
python tools/vocab_convert.py input.json sdcard_template/vocabulary/books/my_book.csv
```

Then copy that file to:

```text
/sdcard/vocabulary/books/
```

## Smoke tests

`smoke_test.py` is retained for the existing serial file-transfer protocol.

`test_parse.py` validates inventory CSV parsing on the PC side.

## Purchase statement converter

`material_to_inventory.py` converts one or more LCSC purchase CSV/ZIP files into a compact UTF-8 `INVENTORY.CSV`, merging duplicate product numbers and summing ordered quantities.
