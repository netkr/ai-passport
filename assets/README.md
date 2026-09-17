<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Pixel subsets for the habit tracker

| File | Format | Use and source |
| --- | --- | --- |
| [`fonts/app_font_12.c`](fonts/app_font_12.c) | LVGL 1 bpp, 12 px, 151 glyphs | Body text, hints and dates. Bitmap data is 1369 bytes. |
| [`fonts/app_font_24.c`](fonts/app_font_24.c) | LVGL 1 bpp, 24 px, 151 glyphs | Category names and the check-in result heading. Bitmap data is 5200 bytes; verified to be an exact 2x integer upscale of `app_font_12` rather than a resampled outline. |
| [`fonts/app_font_charset.txt`](fonts/app_font_charset.txt) | UTF-8 text, 56 code points | The CJK inventory. Derived from the UI text source `main/habit_strings.h`; regenerate it whenever that file changes. |

Both fonts contain printable ASCII `U+0020`-`U+007E` plus the inventory above.

- **Source font:** Ark Pixel Font, 12 px proportional, Simplified Chinese subset, release `2026.09.01`, from `https://github.com/TakWolf/ark-pixel-font`. The source file is `ark-pixel-12px-proportional-zh_cn.ttf` from asset `ark-pixel-font-12px-proportional-ttf-v2026.09.01.zip`. It is intentionally **not committed** (the release archive is 33 MB); download it with `gh release download 2026.09.01 --repo TakWolf/ark-pixel-font --pattern "*12px-proportional-ttf-v*"`.
- **License:** SIL Open Font License 1.1, copyright (c) 2021 TakWolf. The archive ships `OFL.txt`; keep the generated subsets under the same license and do not use the font name as the generated symbol name.
- **Converter:** `lv_font_conv` 1.5.3, pinned. Run without a global install:
  - `--size 12 --lv-font-name app_font_12 --output assets/fonts/app_font_12.c`
  - `--size 24 --lv-font-name app_font_24 --output assets/fonts/app_font_24.c`
  - shared flags: `--range 0x20-0x7F --symbols "$(tr -d '\n' < assets/fonts/app_font_charset.txt)" --bpp 1 --format lvgl --no-compress --no-kerning --lv-include lvgl.h`
- **Do not switch to the 16 px release.** Its CJK coverage is incomplete: of `U+4E00`-`U+4EFF` only 43 of 256 code points are present, and the same holds for the monospaced TTF and the proportional OTF. The 12 px release covers 255 of 256. The 24 px subset is produced by rasterizing the 12 px outline at twice the size, which is exact because these glyph outlines are axis-aligned rectangles on the 12 px grid.
- **Integration:** registered through `target_sources` in `main/CMakeLists.txt`; select the font on the widget that draws the text, not on the theme.
- **Flash cost:** 6.6 KB of bitmap data for both subsets. No runtime allocation; no PSRAM is required.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Hardware overview infographic embedded in both `docs/README.md` files. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
