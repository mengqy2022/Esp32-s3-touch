# Vocab Lab

## 1. Goals

The feature is designed for a small offline ESP32 touch device rather than as a clone of a phone app. The retained learning concepts are:

- wordbooks
- daily plan
- new words + review
- word/phonetic/meaning card
- spelling/dictation exercise
- per-book progress
- offline use after download

Commercial courses, memberships, copyrighted learning images and proprietary wordbooks are deliberately excluded.

## 2. SD paths

```text
/sdcard/vocabulary/
├── books/       final UTF-8 CSV wordbooks
├── progress/    one binary .vcp file per selected wordbook
└── cache/       temporary .json.part downloads
```

The firmware creates these directories automatically after the SD card is mounted.

## 3. CSV schema

Header aliases are supported:

| Logical field | Accepted header examples | Required |
|---|---|---|
| word | `word`, `headword` | yes |
| phonetic | `phonetic`, `ipa` | no |
| meaning | `meaning`, `meaning_cn`, `translation` | yes |
| example | `example`, `examples`, `sentence` | no |

Recommended canonical form:

```csv
word,phonetic,meaning,example
ability,/əˈbɪləti/,能力；才能,Practice improves your ability.
```

CSV quoting and doubled quotes are handled. Keep each logical row on one physical line; the embedded parser has a 1024-byte line buffer.

## 4. Built-in download catalog

`vocabulary.c` contains catalog entries pointing to the public `grhliu/wordtyper-vocabularies` repository. Current entries cover CET‑4, CET‑6, postgraduate entrance exam, TOEFL, IELTS, GRE, GMAT and SAT variants.

Download flow:

```text
GitHub raw JSON
  -> /sdcard/vocabulary/cache/<id>.json.part
  -> streaming line parser
  -> /sdcard/vocabulary/books/<id>.csv.tmp
  -> atomic-ish rename to <id>.csv
  -> delete JSON cache
```

Only a 2 KB HTTP buffer plus one JSON line is needed in RAM.

## 5. Selecting a book

`vocab_select_book()`:

1. verifies a safe file name (no `/` or `..`)
2. validates/counts CSV word rows
3. stores the active filename in NVS
4. opens or creates its progress file
5. refreshes statistics

The wordbook itself remains on SD; only the active file name and count are retained in RAM.

## 6. Progress file

Each book gets:

```text
/sdcard/vocabulary/progress/<book>.vcp
```

The file begins with a versioned header followed by one fixed-size record per word. A record stores:

- due day
- times seen
- SRS box (0..5)
- flags

The header stores aggregate counters such as correct, wrong, learned and today's completed count.

If the word count or record format no longer matches, the firmware recreates the progress file for that book.

## 7. Study session policy

Maximum in-memory session size: **80** words.

### Learn

1. due reviews first
2. fill remaining slots with unseen words
3. session size is the daily target (5..80 in practice; the setting UI supports up to 100, but an individual RAM session caps at 80)

### Dictation

1. due learned words first
2. fill with other learned words
3. on a brand-new book, allow new words as a fallback so dictation is never a dead end

## 8. Lightweight SRS

Correct answers advance the box up to 5. The current due intervals are intentionally simple:

```text
box 1: same day / immediate reinforcement
box 2: +1 day
box 3: +3 days
box 4: +7 days
box 5: +14 days
```

A wrong answer returns the word to box 1 and makes it due immediately.

This is a compact embedded-device policy, not a claim to reproduce any commercial application's algorithm.

## 9. Dictation UI

The dictation screen shows:

- Chinese meaning prompt
- one-line text area
- `CHECK` button
- full-width LVGL lowercase keyboard

First tap on `CHECK`:

- compares case-insensitively after trimming whitespace
- persists correct/wrong progress
- shows `Correct` or the expected spelling
- button changes to `NEXT`

Second tap advances to the next item.

## 10. PC conversion tool

Convert a WordTyper-style JSON file:

```bash
python tools/vocab_convert.py cet4.json sdcard_template/vocabulary/books/cet4.csv
```

Convert a CSV with compatible aliases:

```bash
python tools/vocab_convert.py my_words.csv sdcard_template/vocabulary/books/my_words.csv
```

The tool never needs to be installed on the ESP32.

## 11. Adding your own online source

Add an entry to `CATALOG[]` in `main/vocabulary.c` only if its JSON is compatible with:

```json
{
  "words": [
    {
      "word": "...",
      "phonetic": "...",
      "translations": ["...", "..."]
    }
  ]
}
```

For a different schema, extend the converter instead of loading a generic JSON DOM on ESP32; streaming parsing is much more memory-efficient for this hardware.
