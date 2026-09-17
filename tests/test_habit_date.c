#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "habit_date.h"

// 期望值由 Python 的 datetime 独立生成(见提交说明),不是从本实现反推的,
// 因此这个测试能真正发现历法换算错误,而不是自我印证。
static void test_epoch_days(void)
{
    const struct { int32_t year, month, day, days; } cases[] = {
        { 1970, 1, 1, 0 },
        { 1999, 12, 31, 10956 },
        { 2000, 1, 1, 10957 },
        { 2024, 2, 29, 19782 },
        { 2026, 9, 17, 20713 },
        { 2026, 12, 31, 20818 },
        { 2100, 3, 1, 47541 },  // 跨 2100 非闰年
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const habit_date_t date = {
            .year = cases[i].year, .month = (uint8_t)cases[i].month, .day = (uint8_t)cases[i].day
        };
        assert(habit_date_to_days(date) == cases[i].days);

        // 往返必须无损,且逐字段相等(避免只比天数掩盖字段错误)。
        const habit_date_t back = habit_date_from_days(cases[i].days);
        assert(back.year == cases[i].year);
        assert(back.month == cases[i].month);
        assert(back.day == cases[i].day);
    }
}

static void test_weekday(void)
{
    // 1970-01-01 是周四 = 以周一为 0 的索引 3。
    assert(habit_date_weekday(0) == 3);
    assert(habit_date_weekday(10957) == 5);   // 2000-01-01 周六
    assert(habit_date_weekday(19782) == 3);   // 2024-02-29 周四
    assert(habit_date_weekday(20713) == 3);   // 2026-09-17 周四
    assert(habit_date_weekday(47541) == 0);   // 2100-03-01 周一
    // 负天数也必须落在 0..6。
    assert(habit_date_weekday(-1) == 2);
    assert(habit_date_weekday(-100) >= 0 && habit_date_weekday(-100) <= 6);
}

static void test_days_in_month(void)
{
    assert(habit_date_days_in_month(2024, 2) == 29);  // 整百年外的闰年
    assert(habit_date_days_in_month(2000, 2) == 29);  // 被 400 整除
    assert(habit_date_days_in_month(2100, 2) == 28);  // 被 100 整除但不被 400
    assert(habit_date_days_in_month(2025, 2) == 28);
    assert(habit_date_days_in_month(2026, 4) == 30);
    assert(habit_date_days_in_month(2026, 12) == 31);
    assert(habit_date_days_in_month(2026, 0) == 0);
    assert(habit_date_days_in_month(2026, 13) == 0);
}

static void test_is_valid(void)
{
    const habit_date_t ok = { .year = 2026, .month = 9, .day = 17 };
    assert(habit_date_is_valid(ok));

    const habit_date_t leap_ok = { .year = 2024, .month = 2, .day = 29 };
    assert(habit_date_is_valid(leap_ok));

    const habit_date_t leap_bad = { .year = 2025, .month = 2, .day = 29 };
    assert(!habit_date_is_valid(leap_bad));

    const habit_date_t apr31 = { .year = 2026, .month = 4, .day = 31 };
    assert(!habit_date_is_valid(apr31));

    const habit_date_t m13 = { .year = 2026, .month = 13, .day = 1 };
    assert(!habit_date_is_valid(m13));

    const habit_date_t d0 = { .year = 2026, .month = 9, .day = 0 };
    assert(!habit_date_is_valid(d0));

    // 超出自持时钟的可用范围要拒绝,避免 UI 上出现调不回去的年份。
    const habit_date_t too_early = { .year = 1999, .month = 12, .day = 31 };
    assert(!habit_date_is_valid(too_early));
    const habit_date_t too_late = { .year = 2100, .month = 1, .day = 1 };
    assert(!habit_date_is_valid(too_late));
}

static void test_logical_day_boundary(void)
{
    const int64_t sep17 = (int64_t)20713 * HABIT_SECS_PER_DAY;

    // 4 点整是当天的开始。
    assert(habit_date_logical_day(sep17 + HABIT_DAY_BOUNDARY_SEC) == 20713);
    // 3:59:59 仍算前一天 —— 这正是把边界推到 4 点要吸收的误差。
    assert(habit_date_logical_day(sep17 + HABIT_DAY_BOUNDARY_SEC - 1) == 20712);
    // 午夜 00:00 属于"前一个逻辑日"。
    assert(habit_date_logical_day(sep17) == 20712);
    // 23:59 仍属于当天。
    assert(habit_date_logical_day(sep17 + HABIT_SECS_PER_DAY - 60) == 20713);

    // 负数墙钟要向下取整,不能因截断而偏一天。
    assert(habit_date_logical_day(0) == -1);
    assert(habit_date_logical_day(-HABIT_SECS_PER_DAY) == -2);
}

static void test_adjust_day(void)
{
    // 普通加减。
    assert(habit_date_adjust((habit_date_t){ .year = 2026, .month = 9, .day = 17 },
                             HABIT_FIELD_DAY, 1).day == 18);
    assert(habit_date_adjust((habit_date_t){ .year = 2026, .month = 9, .day = 17 },
                             HABIT_FIELD_DAY, -1).day == 16);

    // 日字段在当月内循环,不进位到月份 —— 这是刻意的语义(见 habit_date.h):
    // 编辑日字段时不应该悄悄改掉月份。
    const habit_date_t end_of_sep = habit_date_adjust(
        (habit_date_t){ .year = 2026, .month = 9, .day = 30 }, HABIT_FIELD_DAY, 1);
    assert(end_of_sep.month == 9 && end_of_sep.day == 1);

    // 往回循环落在当月最后一天:10 月是 31 天,所以 1 日减一天是 31 日。
    const habit_date_t start_of_oct = habit_date_adjust(
        (habit_date_t){ .year = 2026, .month = 10, .day = 1 }, HABIT_FIELD_DAY, -1);
    assert(start_of_oct.month == 10 && start_of_oct.day == 31);

    // 9 月只有 30 天,同一操作给出 30 日 —— 循环边界随月份长度变化。
    const habit_date_t start_of_sep = habit_date_adjust(
        (habit_date_t){ .year = 2026, .month = 9, .day = 1 }, HABIT_FIELD_DAY, -1);
    assert(start_of_sep.month == 9 && start_of_sep.day == 30);

    // 2 月长度按闰年变化:闰年 29 日加一天回到 1 日。
    const habit_date_t feb_end = habit_date_adjust(
        (habit_date_t){ .year = 2024, .month = 2, .day = 29 }, HABIT_FIELD_DAY, 1);
    assert(feb_end.month == 2 && feb_end.day == 1);
}

static void test_adjust_month_clamps_day(void)
{
    // 这是最容易出错的一处:1月31日 加一月不能变成 2月31日。
    const habit_date_t jan31 = { .year = 2026, .month = 1, .day = 31 };
    const habit_date_t feb = habit_date_adjust(jan31, HABIT_FIELD_MONTH, 1);
    assert(feb.month == 2 && feb.day == 28);

    // 闰年同一步必须给出 29。
    const habit_date_t leap_feb =
        habit_date_adjust((habit_date_t){ .year = 2024, .month = 1, .day = 31 },
                          HABIT_FIELD_MONTH, 1);
    assert(leap_feb.month == 2 && leap_feb.day == 29);

    // 3月31日 回退到 2 月同样要收敛。
    const habit_date_t back =
        habit_date_adjust((habit_date_t){ .year = 2026, .month = 3, .day = 31 },
                          HABIT_FIELD_MONTH, -1);
    assert(back.month == 2 && back.day == 28);

    // 月份循环。
    const habit_date_t roll =
        habit_date_adjust((habit_date_t){ .year = 2026, .month = 12, .day = 15 },
                          HABIT_FIELD_MONTH, 1);
    assert(roll.month == 1 && roll.day == 15 && roll.year == 2026);

    const habit_date_t wrap_back =
        habit_date_adjust((habit_date_t){ .year = 2026, .month = 1, .day = 1 },
                          HABIT_FIELD_MONTH, -1);
    assert(wrap_back.month == 12 && wrap_back.day == 1);
}

static void test_adjust_year_cycles_and_clamps(void)
{
    // 年份在支持范围内循环,不在边界卡死。
    const habit_date_t top =
        habit_date_adjust((habit_date_t){ .year = HABIT_YEAR_MAX, .month = 6, .day = 15 },
                          HABIT_FIELD_YEAR, 1);
    assert(top.year == HABIT_YEAR_MIN && top.month == 6 && top.day == 15);

    const habit_date_t bottom =
        habit_date_adjust((habit_date_t){ .year = HABIT_YEAR_MIN, .month = 6, .day = 15 },
                          HABIT_FIELD_YEAR, -1);
    assert(bottom.year == HABIT_YEAR_MAX);

    // 闰日跨到非闰年必须收敛,否则会产生 2025-02-29。
    const habit_date_t leap_day =
        habit_date_adjust((habit_date_t){ .year = 2024, .month = 2, .day = 29 },
                          HABIT_FIELD_YEAR, 1);
    assert(leap_day.year == 2025 && leap_day.month == 2 && leap_day.day == 28);
}

int main(void)
{
    test_epoch_days();
    test_weekday();
    test_days_in_month();
    test_is_valid();
    test_logical_day_boundary();
    test_adjust_day();
    test_adjust_month_clamps_day();
    test_adjust_year_cycles_and_clamps();
    return 0;
}