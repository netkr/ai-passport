<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 打卡应用的点阵子集

| 文件 | 格式 | 用途与来源 |
| --- | --- | --- |
| [`fonts/app_font_12.c`](fonts/app_font_12.c) | LVGL 1 bpp，12 px，151 个字形 | 正文、提示与日期。位图数据 1369 字节。 |
| [`fonts/app_font_24.c`](fonts/app_font_24.c) | LVGL 1 bpp，24 px，151 个字形 | 类别名与打卡结果标题。位图数据 5200 字节；已逐一验证为 `app_font_12` 的**精确 2 倍整数放大**，而不是轮廓字体在 24 px 下的重采样。 |
| [`fonts/app_font_charset.txt`](fonts/app_font_charset.txt) | UTF-8 文本，56 个码点 | 汉字清单。由 UI 文案唯一来源 `main/habit_strings.h` 派生；该文件变动后必须重新生成字体。 |

两个字库都包含可打印 ASCII `U+0020`-`U+007E` 加上上表的清单。

- **源字体：** 方舟像素字体（Ark Pixel Font）12 px 比例宽度、简体中文子集，发布版 `2026.09.01`，来自 `https://github.com/TakWolf/ark-pixel-font`。源文件为资源包 `ark-pixel-font-12px-proportional-ttf-v2026.09.01.zip` 中的 `ark-pixel-12px-proportional-zh_cn.ttf`。该源文件**刻意不入库**（资源包 33 MB）；用 `gh release download 2026.09.01 --repo TakWolf/ark-pixel-font --pattern "*12px-proportional-ttf-v*"` 获取。
- **许可：** SIL Open Font License 1.1，版权归 2021 TakWolf。资源包内附 `OFL.txt`；生成的子集须沿用同一许可，且不要把字体名用作生成符号名。
- **转换器：** `lv_font_conv` 1.5.3，版本固定。无需全局安装即可运行：
  - `--size 12 --lv-font-name app_font_12 --output assets/fonts/app_font_12.c`
  - `--size 24 --lv-font-name app_font_24 --output assets/fonts/app_font_24.c`
  - 共用参数：`--range 0x20-0x7F --symbols "$(tr -d '\n' < assets/fonts/app_font_charset.txt)" --bpp 1 --format lvgl --no-compress --no-kerning --lv-include lvgl.h`
- **不要改用 16 px 版本。** 它的汉字覆盖不完整：`U+4E00`-`U+4EFF` 区间 256 个码点只命中 43 个，等宽 TTF 与比例 OTF 同样如此；12 px 版本命中 255 个。24 px 子集由 12 px 轮廓以两倍尺寸栅格化得到，之所以精确，是因为这些字形轮廓是 12 px 网格上的轴对齐矩形。
- **集成方式：** 通过 `main/CMakeLists.txt` 的 `target_sources` 挂载；字体要设在**实际绘制文本的控件**上，设在主题或屏幕上是无效的。
- **Flash 开销：** 两个子集的位图合计 6.6 KB。无运行时分配，无需 PSRAM。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 嵌入中英文 `docs/README.md` 的硬件概览图。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
