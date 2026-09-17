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

int main(void)
{
    test_epoch_days();
    test_weekday();
    test_days_in_month();
    test_is_valid();
    test_logical_day_boundary();
    return 0;
}