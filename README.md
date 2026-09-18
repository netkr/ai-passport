<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pixel Habit Tracker

An offline pixel-art habit check-in app built on the
[FoloToy AI Passport](https://github.com/FoloToy/ai-passport) (ESP32-C3, 240×320,
three buttons). This repository is my fork of that project; the work here is the
application itself, not the upstream baseline.

- Four daily habits: 早睡 · 锻炼 · 戒烟 · 阅读
- Month-view calendar per habit, 90 days of rolling history
- Three-second undo after a mis-tap
- NVS persistence across power loss, deep-sleep wake with date continuity
- No network, no account, no companion app

## Documentation

| | |
| --- | --- |
| [Application guide](docs/apps/pixel-habit-tracker.md) | Controls, storage model, power design, source layout |
| [Hardware guide](docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) | Board facts and BSP contract |
| [Build & test](docs/development/engineering/build-and-test.md) | Validate, flash, and accept on device |

## Build

```bash
source $IDF_PATH/export.sh   # ESP-IDF 5.5.3
./tools/validate.sh --static     # host tests
./tools/validate.sh --firmware  # build + merged-image check
```

The application lives on the `feature/pixel-habit-tracker` branch; `main` stays
as the upstream baseline.

---

Forked from [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport).
MIT License.
