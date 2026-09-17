#!/usr/bin/env python3
"""The generated CJK font subsets must cover every character the UI can show.

`main/habit_strings.h` is the single source of on-screen text. The glyph subset in
`assets/fonts/` is generated from that file by hand, so a string edit without a
regeneration would only surface as missing glyphs on the device. These checks move that
failure into the host test stage.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]

STRINGS = ROOT / "main/habit_strings.h"
CHARSET = ROOT / "assets/fonts/app_font_charset.txt"
FONTS = ("assets/fonts/app_font_12.c", "assets/fonts/app_font_24.c")

# 文案宏的右值:只取定义行里的字符串字面量,注释里的中文不要求字体覆盖。
LITERAL_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
CMAP_ENTRY_RE = re.compile(r"\{[^{}]*\.range_start[^{}]*\}", re.S)

# 解析失败会让测试变成空转,所以给一个"确实读到了真东西"的下限。
MIN_LITERALS = 10
MIN_CJK = 20

CJK_START = 0x2E80


def ui_characters() -> set[str]:
    source = STRINGS.read_text(encoding="utf-8")
    define_lines = [
        line for line in source.splitlines() if line.lstrip().startswith("#define")
    ]
    literals = LITERAL_RE.findall("\n".join(define_lines))
    assert len(literals) >= MIN_LITERALS, (
        f"only {len(literals)} string literals parsed from {STRINGS.name}"
    )
    return {ch for literal in literals for ch in literal}


def charset_inventory() -> set[str]:
    return set(CHARSET.read_text(encoding="utf-8").replace("\n", ""))


def font_code_points(relative_path: str) -> set[int]:
    """Every code point the generated font can draw, from its cmap tables."""
    source = (ROOT / relative_path).read_text(encoding="utf-8")
    covered: set[int] = set()
    entries = CMAP_ENTRY_RE.findall(source)
    assert entries, f"no cmap entries found in {relative_path}"
    for entry in entries:
        start = int(re.search(r"\.range_start = (\d+)", entry).group(1))
        length = int(re.search(r"\.range_length = (\d+)", entry).group(1))
        unicode_list = re.search(r"\.unicode_list = (\w+)", entry).group(1)
        if unicode_list == "NULL":
            # 紧凑区段:整个区间都有字形(ASCII 就是这种)。
            covered.update(range(start, start + length))
            continue
        # 稀疏区段:列表存的是相对 range_start 的偏移。
        array = re.search(
            rf"static const uint16_t {unicode_list}\[\] = \{{(.*?)\}};", source, re.S
        )
        assert array, f"{relative_path}: missing array {unicode_list}"
        covered.update(
            start + int(value, 0)
            for value in re.findall(r"0x[0-9A-Fa-f]+|\d+", array.group(1))
        )
    return covered


class FontCoverageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ui = ui_characters()
        cls.cjk = {ch for ch in cls.ui if ord(ch) >= CJK_START}

    def test_ui_strings_contain_the_expected_inventory(self) -> None:
        self.assertGreaterEqual(len(self.cjk), MIN_CJK)

    def test_charset_inventory_matches_the_ui_strings(self) -> None:
        # 字符清单是生成字体的输入,必须与文案严格同步:多了说明有废字,
        # 少了说明改了文案却忘了重新提取与生成。
        self.assertEqual(charset_inventory(), self.cjk)

    def test_every_font_covers_every_ui_character(self) -> None:
        for relative_path in FONTS:
            with self.subTest(font=relative_path):
                covered = font_code_points(relative_path)
                missing = sorted(ch for ch in self.ui if ord(ch) not in covered)
                self.assertEqual(
                    missing,
                    [],
                    f"{relative_path} cannot draw {missing}; regenerate the fonts",
                )

    def test_fonts_stay_one_bit_per_pixel(self) -> None:
        # 像素风依赖 1bpp 未压缩点阵;重新生成时若漏掉 --bpp 1,字形会静默变化。
        for relative_path in FONTS:
            with self.subTest(font=relative_path):
                source = (ROOT / relative_path).read_text(encoding="utf-8")
                self.assertIn(".bpp = 1", source)


if __name__ == "__main__":
    unittest.main()