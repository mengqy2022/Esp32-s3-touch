# Contributing

Thanks for your interest in the project! It is a small ESP-IDF firmware
toolbox, so contributions are welcome in any form: bug reports, feature
requests, documentation fixes or pull requests.

## Ground rules

- **Small, focused changes.** One logical change per pull request; describe
  what and why in the PR body.
- **Stay ASCII/UTF-8.** Sources, CSVs and scripts are UTF-8. Screen text must
  stick to the glyphs the bundled Montserrat font ships (`0x20-0x7F` plus
  U+00B0 and U+2022; U+00B7 for example renders as a box).
- **Keep it offline-first.** The device must keep working without a network
  or a host PC; new features that need a companion tool must degrade
  gracefully.
- **RAM is scarce.** ESP32-WROOM-32E has no PSRAM — prefer streaming over
  `fread` bursts, fixed-record state over big JSON rewrites, and small task
  stacks.
- **Do not bundle commercial assets.** Wordbooks, images, audio or courses
  from commercial products must not be committed (see `THIRD_PARTY_NOTICES.md`).

## Development

```bash
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

CI builds the `esp32` target with ESP-IDF `v5.5.4` on every push
(`.github/workflows/build.yml`). Run the same version locally to reproduce CI.

## Adding a new module

1. Add a screen id to `main/ui.h` (`ui_screen_t`).
2. Add the menu card + case in `main/ui.c` (`build_menu_screen()`,
   `on_menu_btn()`).
3. Add the screen builder/refresher and wire it into `do_draw_all()`,
   `do_update_status()` and `ui_timer_cb()`.
4. Never touch LVGL objects from another task — post a command with
   `lv_port_post_cmd()` instead.
5. Document the feature in `README.md`, `CHANGELOG.md` and, if it speaks a
   serial protocol, in `docs/PROTOCOL.md`.

## Style

- Follow the surrounding code (C99/GNU17, snake_case, `static` where
  possible, `ESP_LOGx`/`ESP_ERROR_CHECK` for the ESP-IDF parts).
- The build treats warnings as errors for most categories; fix them at
  source.
- Python tools: Python 3, `pyserial` only, UTF-8 safe printing.

## License

By contributing you agree that your contributions are licensed under the
project license (see `LICENSE`).