<p align="right">
  <a href="pixel-habit-tracker.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pixel Habit Tracker

An offline, pixel-art habit check-in application for the FoloToy AI Passport (ESP32-C3).
Six daily habits, three buttons, no network, no account, no companion app, and records
that survive a power loss.

This application is a fork-local product: it was designed from the board's BSP and demos
rather than by reusing the demo menu or its visual shell, and it does not change the
upstream hardware layer. On-screen text is Simplified Chinese.

## Tracking model

| Item | Behaviour |
| --- | --- |
| Habits | Early sleep, exercise, quitting smoking, drinking water, reading, and waking up early |
| Check-in | One per habit per logical day; a repeat is refused instead of overwritten |
| Records | 90 ring slots addressed by `day % 90`, each carrying its own date and a 6-bit mask |
| Streak | Consecutive days for one habit, counted back from today; a day still in progress does not break it |
| Day boundary | 04:00, so a check-in at 03:59 belongs to the previous day |
| Storage | NVS, field-by-field little-endian with magic, version, slot-count, and length checks; anything that fails is reported as "no records" instead of being guessed |

Habit order is part of the stored format, because a habit's position is the bit index in
each day's mask. New habits are therefore appended: the six habits share one byte, and the
three original ones keep the low bits they have always occupied, so records written by an
earlier firmware still describe the same habits. Eight habits is the ceiling for this
format, and a `_Static_assert` turns a wider set into a build error rather than a silent
format change.

## Controls

| Key | Main menu | Confirmation window | Result page | Month view | Date page |
| --- | --- | --- | --- | --- | --- |
| UP / DOWN click | move between habits | — | — | previous / next month | adjust the highlighted field |
| OK click | open the confirmation window | check in | undo the check-in (within 3 s), otherwise back to the menu | next habit | next field, then confirm the date |
| OK long press | open the month view | back to the menu | keep the check-in and go back | back to the menu | back (not during first-time setup) |
| Any key, screen blanked | light the screen; that press is consumed and does not reach the interface | | | | |

The main menu shows three rows at a time and scrolls a window over the habit list, so the
number of on-screen objects stays fixed no matter how many habits exist. A line under the
header shows today's completion ("2/6") on the left and the selected habit's streak on the
right; the selection wraps at both ends while the window itself stops at the list edges, so
no empty row is ever shown.

## Undoing a mis-tap

A check-in is one press away, so the success page doubles as a three-second undo window:
while it is up, pressing OK removes that habit's check-in for today and returns to the
menu, and long-pressing OK keeps the check-in and returns. The window can only be used
once, and the undo is written to flash — otherwise the check-in would reappear on the next
boot.

Undo is deliberately silent. The visible result is the feedback: the row loses its filled
marker and the completion counter drops. A rising tone would read as "achieved", which is
the opposite of what happened. The undo hint only appears on a page that can actually be
undone, so the "today already checked in" page never invites a press that would do nothing.

## Month view

Long-pressing OK opens a calendar for one habit at a time: the month and year as the
heading, the habit and its streak underneath, then a seven-column grid with one numbered
cell per day. A checked-in day is filled green, today gets a yellow outline, and padding
cells outside the month are hidden. Pressing OK cycles through the six habits.

The grid is always seven columns by six rows — the smallest fixed shape that fits any
month, because a 31-day month starting on a Sunday occupies 37 cells. Switching months or
habits therefore only rewrites the 42 existing cells instead of rebuilding the page.

Navigation stops at both ends rather than wrapping: the newest month is the current one,
and the oldest is the month containing `today - 89`, which follows from the 90 ring slots.
Earlier days are no longer in the store, so paging further back could only show an empty
grid and would look like data loss. On a fresh device the streak line reads "no records
yet" instead of "0 consecutive days".

A successful check-in turns the result card green and plays two rising square-wave notes;
a refused repeat plays one low note and writes nothing to flash. A failed NVS write is
reported on screen and with the low note, so a lost record is never silent.

## Date without a clock source

The board has no network and no 32.768 kHz crystal, so nothing on it supplies absolute
time. The user sets the date once, and from then on the application advances it with the
RTC counter, which keeps counting through deep sleep and resets only when power is lost.

A time anchor kept in RTC memory decides whether the stored date can still be trusted: it
survives deep sleep and resets, and disappears on power loss. When the anchor is gone the
application does not pretend to know the date — it asks the user to confirm or correct it
before showing the menu. The last known wall clock is stored in NVS as well, so that
confirmation starts from roughly the right date instead of a fixed default.

## Power

Idle time is measured from the last key press, in two stages:

- After 15 s the backlight goes off. The MCU keeps running, so the next key press restores
  the screen instantly instead of paying for an application restart.
- After 30 s the application sleeps. Before it does, it arms the wake source, stops the
  tone task, freezes input, flushes the last known date, and then shuts down the CW2017
  gauge, the ES8311 codec, the I2S pins, the shared I2C bus, and the panel, in that order.

All three keys wake the device: they share one ADC node with an external pull-up, so any
key pulls that node low. The ESP32-C3 has no EXT0/EXT1 wakeup, so the firmware arms a GPIO
wake source instead, and treats a failure to arm it as a reason not to sleep at all —
sleeping without a wake source means the device can only be recovered by power cycling.

Deep-sleep wake restarts the application. The date is reconstructed from the RTC anchor, so
it stays continuous, and the press that woke the device is deliberately not delivered to
the interface.

## Hardware used

| Part | Interface | Used for |
| --- | --- | --- |
| ST7789P3, 240 × 320 | SPI2 + LEDC backlight | the only user interface |
| ES8311 | shared I2C + I2S0 | two check-in tones |
| CW2017 | shared I2C, address `0x63` | battery percentage in the header (hidden when unreadable) |
| Three-button ladder | ADC1 channel 0 on GPIO0 | navigation, check-in, back, and deep-sleep wake |

Pin numbers, voltage windows, and register details belong to
[`components/bsp/include/bsp_pins.h`](../../components/bsp/include/bsp_pins.h) and the
[hardware development guide](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md); the
application contains no pin constants of its own.

## Source layout

| File | Role |
| --- | --- |
| `main/main.c` | minimal entry point |
| `main/habit_app.c` | event task, NVS writes, wall-clock base, idle timing, deep-sleep sequence |
| `main/habit_ui.c` | the five pixel-art pages, including the scrolling menu and the month grid |
| `main/habit_strings.h` | the single source of on-screen text |
| `main/habit_audio.c` | tone generation on its own task, with the suspend handshake used before sleep |
| `main/habit_device.c` | the only file that touches the RTC counter, RTC memory, and NVS |
| `main/habit_date.c`, `habit_model.c`, `habit_clock.c`, `habit_store.c` | pure C logic and encodings, covered by host tests. `habit_date.c` carries the month cursor the calendar pages through. |

Concurrency is deliberately narrow: button and timer callbacks only enqueue events, and one
task owns the interface, the records, and every storage write.

## Build, flash, and test

```bash
source $IDF_PATH/export.sh
./tools/validate.sh --static     # repository checks + host tests
./tools/validate.sh --firmware   # ESP-IDF build + merged-image verification
./tools/validate.sh              # both
```

`tools/validate.sh` builds into a temporary directory and installs
`build/FoloToy-AI-Passport-full.bin`, the merged image for offset `0x0`. Flashing that image
resets NVS. To keep the stored records and the date anchor, flash the application partition
instead — the app-only image is kept in the debug archive:

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 --before default_reset \
  --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x10000 build/firmware/<image-sha256>/FoloToy-AI-Passport.bin
```

Two console facts matter when reading logs over USB Serial/JTAG: the port disappears while
the chip is in deep sleep, and the first lines of a boot can be lost while the interface
enumerates. That is why the application's ready line carries the wake cause instead of
logging it as the very first line of `app_main`.

## Fonts

The interface uses Ark Pixel, licensed under OFL-1.1, at 12 px and 24 px. Text lives only in
`main/habit_strings.h`; the glyph subset is generated from that file. Changing any on-screen
string therefore requires regenerating the fonts — the procedure and the exact
`lv_font_conv` flags are documented in [`assets/README.md`](../../assets/README.md). The
host test `tests/test_app_font_coverage.py` keeps that honest: it derives the CJK inventory
from the text header and fails if the charset list or either generated subset does not
cover every character the interface can print.

## Verification status

Observed on hardware: Simplified Chinese pixel text renders correctly; the application
reaches deep sleep about 30 s after the last key press (the serial port drops); any key
wakes it, twice in a row, with the date still trusted afterwards.

Not yet confirmed on hardware: the 15 s backlight blanking (visible only to the eye, it
leaves no log), the duplicate-check-in refusal, the tones, and the numeric wake-cause value
in the ready line.

The six-habit release has not been run on hardware at all yet. Specifically unverified:
the scrolling three-row window at both ends of the list and its wrap-around, the today
counter and the streak line tracking the selection, and an upgrade in which records written
by the three-habit firmware are read back after flashing the application partition.

The month view is also unrun on hardware: the weekday columns lining up with real dates
across months that need five versus six rows, the today outline, the month clamp at both
ends, the habit cycle, and whether a screen of 42 cells plus 42 labels renders without a
visible delay on a key press.

The undo window is unrun on hardware as well: undoing inside the three seconds and seeing
the check-in stay gone after a reboot, letting the window expire and seeing the check-in
kept, and long-pressing OK during the window without losing the check-in.