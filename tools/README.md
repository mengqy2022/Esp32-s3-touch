# PC tools

## Inventory serial tool

`inventory_tool.py` manages the existing inventory/BOM workflow over the ESP32 USB serial connection. It can list, upload, download and delete SD files and merge supported inventory CSV formats.

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
