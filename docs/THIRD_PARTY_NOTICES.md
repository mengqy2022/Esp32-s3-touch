# Third-party notices / publishing checklist

This file is intentionally a practical notice, not legal advice.

## LVGL

The firmware uses LVGL through ESP-IDF Component Manager:

- Project: https://github.com/lvgl/lvgl
- Version requested: `~8.4.0`
- Upstream license: MIT (see the exact version downloaded by Component Manager)

`managed_components/` is not committed in this package.

## Remote vocabulary data

The built-in Vocab Lab catalog points to:

- https://github.com/grhliu/wordtyper-vocabularies

That repository states that its wordbooks are objectively derived from ECDICT under the MIT License and that protected content from commercial dictionaries / vocabulary books is not included. The firmware downloads these files at user request; the large wordbooks themselves are not bundled in this project archive.

Before public/commercial distribution, keep the upstream attribution and re-check the upstream repository's current `LICENSE` and `NOTICE.md`.

## Baicizhan / 百词斩

The name is mentioned only as the user's product-reference point for learning workflow ideas. This project does **not** bundle or claim affiliation with Baicizhan and does not copy its commercial assets, copyrighted images, proprietary wordbooks, courses, memberships or source code.

## Existing embedded CJK glyph table

`main/font_cn16.c` is an embedded C-source glyph table that already existed in the supplied firmware and is required for Chinese text rendering. The raw desktop font file and font-generation toolchain are deliberately excluded from this GitHub package.

If you plan to publish or commercially redistribute the repository, independently verify that you have redistribution rights for the embedded glyph data. A conservative future improvement is to regenerate this C table from an openly licensed CJK typeface (for example an OFL-licensed Noto CJK font) and retain that font's required license notice.

## Project-level license

No project-level `LICENSE` was selected automatically. Add one only after deciding how you want others to use your own firmware code.
