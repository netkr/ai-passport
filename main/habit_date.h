// main/habit_date.h —— 纯日期运算,不依赖 libc time / ESP-IDF,可直接在主机上测。
//
// 为什么自己写而不是用 time.h:
//   本板没有可用的绝对时间源(无网络校时、无 32.768kHz 晶振),墙钟由应用自己
//   维护 —— 用户在设置页给定日期,之后靠 RTC 计时器累计。既然后者已经是"自持"
//   的秒数,就没有必要再引入 newlib 的时区/localtime 机制,也就避免了主机测试
//   依赖系统时区(那会让测试结果随机器变化)。
//
// 日界定在凌晨 4 点:见 HABIT_DAY_BOUNDARY_SEC。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 逻辑日边界(相对本地 00:00 的秒数)。
//
// 取 4 点而不是 0 点的原因:本板 RTC 走内部 RC 振荡器,CJK 长期走时有漂移。
// 若以 0 点为界,漂移几十分钟就可能把 23:50 的打卡记到第二天;把边界推到凌晨
// 4 点后,这类误差被完全吸收 —— 深夜打卡仍归属"刚过去的那个白天"。
#define HABIT_DAY_BOUNDARY_SEC (4 * 3600)
#define HABIT_SECS_PER_DAY 86400

// 支持范围。窄范围是刻意的:超出后 UI 的年/月/日选择器要调很多下,
// 且本产品的使用场景(日常打卡)不需要更宽。
#define HABIT_YEAR_MIN 2000
#define HABIT_YEAR_MAX 2099

typedef struct {
    int32_t year;
    uint8_t month;  // 1..12
    uint8_t day;    // 1..31
} habit_date_t;

// 与 1970-01-01 相差的天数(proleptic Gregorian)。1970-01-01 为 0。
int32_t habit_date_to_days(habit_date_t date);
habit_date_t habit_date_from_days(int32_t days);

bool habit_date_is_valid(habit_date_t date);
int32_t habit_date_days_in_month(int32_t year, int month);

// 0 = 周一 .. 6 = 周日。
int8_t habit_date_weekday(int32_t days);

// 日期设置页可调整的字段。
typedef enum {
    HABIT_FIELD_YEAR = 0,
    HABIT_FIELD_MONTH,
    HABIT_FIELD_DAY,
} habit_date_field_t;

// 按 delta(通常 ±1)调整某个字段,越界时**在该字段内循环**,不进位到上位字段:
//   日:9月30日 加一天 → 9月1日(不会变成 10月1日)
//   月:12月 加一月 → 次年之外的 1月(年不变,月自身循环)
//   年:HABIT_YEAR_MAX 加一年 → HABIT_YEAR_MIN
//
// 这样三个字段彼此独立,用户在编辑日字段时不会意外改掉月份。代价是不能靠
// 连续按"下"走完一整年 —— 但设置日期时月份本来就用月字段跳,不依赖日的进位。
//
// 唯一例外是"收敛":调整年或月之后,日会按新月份的长度收缩,因为 2月31日
// 这种日期不存在,无从循环。
//   1月31日 加一月 → 2月28日(闰年 2月29日)
//   2024-02-29 加一年 → 2025-02-28
habit_date_t habit_date_adjust(habit_date_t date, habit_date_field_t field, int delta);

// 墙钟秒 → 逻辑日(已计入 4 点边界)。返回值即 habit_date_to_days 的刻度,
// 因此可以直接和记录槽里的 day 比较。
int32_t habit_date_logical_day(int64_t wall_sec);