// main/habit_strings.h —— 应用全部可见文案的唯一来源(single source of truth)。
//
// 为什么单独抽一个头文件:
//   1. 中文 UI 的字体子集必须覆盖这里出现的每一个字符。文案散落在各页代码里,
//      就没有办法用自动化检查保证"字体覆盖了界面上所有会显示的字"。
//   2. tests/test_app_font_coverage.py 直接解析本文件,提取全部 CJK 字符,与
//      assets/fonts/app_font_charset.txt 及两份生成字体的 unicode 表比对 ——
//      改了文案却忘了重新生成字体,会在主机测试阶段就失败,而不是等到真机上
//      看到方块。
//
// 改动本文件后必须重新生成字体,步骤见 assets/README.md 的 Fonts 一节。
#pragma once

// ---------------------------------------------------------------------------
// 打卡类别。顺序即主菜单显示顺序,也是记录位图里 bit 的顺序(model 侧依赖)。
// 新增类别只能追加在末尾 —— 插在中间会让已存的老记录被解释成别的项目。
// ---------------------------------------------------------------------------
#define HABIT_LABEL_SLEEP    "早睡"
#define HABIT_LABEL_EXERCISE  "锻炼"
#define HABIT_LABEL_QUIT      "戒烟"
#define HABIT_LABEL_READ      "阅读"

// ---------------------------------------------------------------------------
// 主菜单
// ---------------------------------------------------------------------------
#define STR_DATE_FORMAT   "%d月%d日 周%s"   // 例:17日 周三
#define STR_BATTERY       "%d%%"
#define STR_DONE_MARK     "已打"
#define STR_TODO_MARK     "未打"
// 顶栏下方那条状态行:左边是当天完成度,右边是选中项的连续天数。
#define STR_TODAY_PROGRESS "今日 %d/%d"
#define STR_STREAK         "连续%d天"
#define STR_MENU_HINT     "上下选择 确定打卡"
#define STR_MENU_HINT_REC "长按看记录"

// ---------------------------------------------------------------------------
// 打卡确认页
// ---------------------------------------------------------------------------
#define STR_TODAY_TODO   "今天未打卡"
#define STR_TODAY_DONE   "今天已打卡"
#define STR_CONFIRM_TODO "确定打卡"
#define STR_BACK_HINT    "长按返回"

// ---------------------------------------------------------------------------
// 打卡结果
// ---------------------------------------------------------------------------
#define STR_CHECKED_IN "打卡成功"
#define STR_ALREADY    "今天已打卡"
// 只在结果页真的可以撤销时显示(见 habit_ui.c 的 result_build)。
#define STR_UNDO_HINT  "确定撤销"

// ---------------------------------------------------------------------------
// 记录页:整月网格
//
// 页面顶部是年月与当前类别,网格里每格一天(已打卡=实心绿),底部是操作提示。
// ---------------------------------------------------------------------------
#define STR_MONTH_FORMAT   "%d年%d月"     // 例:2026年9月
// 操作提示分两行:换月与换项在同一行,返回单独一行(底部只有两行的空间)。
#define STR_CAL_HINT       "上下换月 确定换项"
// 一次记录都还没有时替代"连续0天"。"连续0天"没错,但读起来像在责备;
// 而且此时用户更需要知道的是"还没开始记",而不是一个零。
#define STR_NO_RECORD      "暂无记录"
// 星期表头。索引 0..6 对应周一到周日。
#define STR_WEEKDAY_1 "一"
#define STR_WEEKDAY_2 "二"
#define STR_WEEKDAY_3 "三"
#define STR_WEEKDAY_4 "四"
#define STR_WEEKDAY_5 "五"
#define STR_WEEKDAY_6 "六"
#define STR_WEEKDAY_7 "日"

// ---------------------------------------------------------------------------
// 日期设置 / 确认页
// ---------------------------------------------------------------------------
#define STR_DATE_SET      "设置日期"
#define STR_DATE_CONFIRM  "确认日期"
#define STR_DATE_SUSPECT  "日期可能不准"
#define STR_UNIT_YEAR     "年"
#define STR_UNIT_MONTH    "月"
#define STR_UNIT_DAY      "日"
#define STR_ADJUST_HINT   "上下调整"
#define STR_NEXT_HINT     "确定下一段"
#define STR_SAVE_FAILED   "保存失败"

// ---------------------------------------------------------------------------
// 存储失败(与 STR_SAVE_FAILED 分开:一个是日期,一个是打卡记录)
// ---------------------------------------------------------------------------
#define STR_RECORD_FAILED "保存失败"