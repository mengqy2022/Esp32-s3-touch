# Vocab Lab V6 behavior

## SD wordbooks and non-blocking import

The device accepts UTF-8 CSV and compatible WordTyper JSON files in:

```text
/sdcard/vocabulary/books/
```

A CSV opens directly. A JSON is converted on SD to the same basename with a `.csv` suffix. Conversion, validation, progress-file preparation, and the first session are performed in a background FreeRTOS task. WORDBOOKS reports import percentage and converted word count so the UI does not appear frozen.

When both `foo.json` and `foo.csv` exist, WORDBOOKS uses/displays the CSV.

## Text encoding and font behavior

Internal copies use UTF-8-aware truncation. This prevents fixed-size C buffers from ending in a partial multi-byte character.

Chinese meaning/example text is checked against the embedded `font_cn16` glyph table before display. Unsupported characters are replaced with `?` rather than being passed to LVGL as missing/invalid glyphs.

The embedded font does not contain a complete IPA set. Common IPA characters are therefore rendered with an ASCII-compatible notation, for example:

```text
ə -> @     ɪ -> I     ʊ -> U     ʌ -> ^
ʃ -> sh    ʒ -> zh    θ -> th    ð -> dh
ŋ -> ng    ˈ -> '     ˌ -> ,     ː -> :
```

## Dictation handwriting

DICTATION offers two input modes:

- `HAND`: touch handwriting pad; draw one uppercase printed A-Z letter and tap `ADD`. The recognized letter is appended lowercase to the answer.
- `KEY`: the existing LVGL lowercase keyboard.

`CLEAR` clears the current drawing and `DEL` removes the last answer character.

The recognizer is a compact 5x7 template classifier intended for an ESP32. It is deliberately per-letter, offline, and lightweight; it is not continuous cursive OCR.

## Daily plan and persistence

Progress is stored per book under:

```text
/sdcard/vocabulary/progress/<book>.vcp
```

V6 uses progress format V2. Each word stores its last study date in addition to SRS state. LEARN therefore counts each unique word at most once toward the current day's goal and can resume the remaining quota later the same day.

LEARN queue policy:

1. due learned words not already studied today;
2. unseen words not already studied today;
3. stop when the remaining daily quota is filled (a single RAM batch is capped at 80; a 100-word day can continue in a second session).

DICTATION draws only from previously learned words and does not consume new-word quota.

V1 progress files are migrated automatically. Aggregate learned/correct/wrong/SRS information is preserved. V1 did not store a per-word study date, so `today_done` is reset once at migration to avoid double counting.

## Date and network behavior

The project defaults to `CST-8` in POSIX TZ notation, which means UTC+8 / China Standard Time. Change `NET_DEFAULT_TZ` in `main/net_utils.c` if the device is used elsewhere.

VOCAB LAB itself is offline-first and does not require Wi-Fi. However, a normal ESP32 without an external RTC cannot know how much calendar time passed while fully powered off. The firmware therefore:

- uses SNTP whenever Wi-Fi is available;
- stores the last trusted local calendar day in NVS;
- never resets the plan to 1970 when network time is absent;
- keeps the cached day until a real clock arrives.

For guaranteed daily rollover after a power-off that may have crossed midnight, connect Wi-Fi and wait for `DATE OK` / `DATE SYNCED` before starting that day's LEARN. `DATE CACHED` is safe for continued offline use on the same known day, but cannot prove that a powered-off device has crossed midnight.
