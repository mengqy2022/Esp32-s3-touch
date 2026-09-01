Vocabulary SD layout
====================
Copy the content of sdcard_template/ to the root of a FAT32 MicroSD card.

Fixed paths used by the firmware:
  /sdcard/vocabulary/books/     UTF-8 CSV wordbooks
  /sdcard/vocabulary/progress/  generated *.vcp progress files
  /sdcard/vocabulary/cache/     temporary download cache

CSV header:
  word,phonetic,meaning,example

Required fields: word, meaning
Optional fields: phonetic, example
UTF-8 is recommended. CSV quoted fields and commas inside quoted fields are supported.

Manual JSON import (firmware V5)
===============================
You may also copy a compatible WordTyper JSON file directly into:
  /vocabulary/books/*.json

Open VOCAB LAB -> WORDBOOKS and tap the JSON book. The firmware will create
a normalized .csv next to it, make that CSV ACTIVE, and enter LEARN. If both
foo.json and foo.csv exist, WORDBOOKS shows/uses foo.csv.

Compatible JSON schema:
  {"words":[{"word":"ability","phonetic":"/.../","translations":["能力"]}]}
