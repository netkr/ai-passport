#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "habit_clock.h"

// 2026-09-17 的逻辑日(与 test_habit_date.c 的参照值一致)。
#define DAY 20713
#define USEC 1000000

static const int64_t WALL_SEP17 =
    (int64_t)DAY * HABIT_SECS_PER_DAY + HABIT_DAY_BOUNDARY_SEC;

static habit_clock_mark_t mark_at(uint64_t rtc_us, int64_t wall_sec)
{
    const habit_clock_mark_t mark = {
        .magic = HABIT_CLOCK_MARK_MAGIC, .reserved = 0,
        .rtc_us = rtc_us, .wall_sec = wall_sec,
    };
    return mark;
}

static void test_certain_after_deep_sleep(void)
{
    // 睡前锚点:墙钟为 9 月 17 日,当时 RTC 计数为 10 秒。
    const habit_clock_mark_t mark = mark_at(10 * USEC, WALL_SEP17);

    // 深睡 5 天后唤醒:RTC 计数继续走,因此能算出真实经过时间。
    const uint64_t wake_rtc = 10 * USEC + (uint64_t)5 * 24 * 3600 * USEC;
    habit_clock_t clock;
    const habit_clock_state_t state =
        habit_clock_boot(&clock, wake_rtc, &mark, true, WALL_SEP17);

    assert(state == HABIT_CLOCK_CERTAIN);
    assert(habit_clock_today(&clock, wake_rtc) == DAY + 5);
    assert(habit_clock_now_sec(&clock, wake_rtc) == WALL_SEP17 + 5 * 24 * 3600);
}

static void test_counter_going_backwards_means_power_loss(void)
{
    // RTC 计数只增不减:锚点记录的计数大于当前值,只可能是掉电后重新计数。
    const habit_clock_mark_t mark = mark_at(90 * USEC, WALL_SEP17);
    habit_clock_t clock;

    const habit_clock_state_t state =
        habit_clock_boot(&clock, 5 * USEC, &mark, true, WALL_SEP17 - 3 * 86400);
    assert(state == HABIT_CLOCK_UNCERTAIN);
    // 沿用 NVS 里的最后已知日期,而不是锚点里那个已经不可信的日期。
    assert(habit_clock_today(&clock, 5 * USEC) == DAY - 3);
}

static void test_bad_magic_is_ignored(void)
{
    habit_clock_mark_t mark = mark_at(10 * USEC, WALL_SEP17);
    mark.magic = 0;

    habit_clock_t clock;
    assert(habit_clock_boot(&clock, 20 * USEC, &mark, true, WALL_SEP17 - 86400)
           == HABIT_CLOCK_UNCERTAIN);
    assert(habit_clock_today(&clock, 20 * USEC) == DAY - 1);
}

static void test_first_boot_is_unset(void)
{
    habit_clock_t clock;
    assert(habit_clock_boot(&clock, 7 * USEC, NULL, false, 0) == HABIT_CLOCK_UNSET);

    // 未设置日期时(today 由 base_wall_sec=0 推出)不能当成有效日期使用;
    // 调用方必须先走设置页。这里只固定该行为,避免悄悄变成某个"合理"日期。
    assert(habit_clock_now_sec(&clock, 7 * USEC) == 0);
}

static void test_set_date_anchors_at_boundary(void)
{
    const habit_date_t date = { .year = 2026, .month = 9, .day = 17 };
    habit_clock_t clock;
    habit_clock_boot(&clock, 1000 * USEC, NULL, false, 0);
    habit_clock_set_date(&clock, 1000 * USEC, date);

    assert(habit_clock_now_sec(&clock, 1000 * USEC) == WALL_SEP17);
    assert(habit_clock_today(&clock, 1000 * USEC) == DAY);

    // 从设定时刻(该日 04:00)起到次日 03:59:59 仍属同一天;
    // 期间经过的午夜并不换日 —— 这正是把边界推到 4 点的意义。
    const uint64_t midnight =
        1000 * USEC + (uint64_t)(HABIT_SECS_PER_DAY - HABIT_DAY_BOUNDARY_SEC) * USEC;
    assert(habit_clock_today(&clock, midnight) == DAY);

    const uint64_t last_second = 1000 * USEC + (uint64_t)(HABIT_SECS_PER_DAY - 1) * USEC;
    assert(habit_clock_today(&clock, last_second) == DAY);

    // 满 24 小时才进入下一个逻辑日。
    const uint64_t next_boundary = 1000 * USEC + (uint64_t)HABIT_SECS_PER_DAY * USEC;
    assert(habit_clock_today(&clock, next_boundary) == DAY + 1);
}

static void test_mark_round_trip(void)
{
    habit_clock_t clock;
    habit_clock_boot(&clock, 0, NULL, false, 0);
    habit_clock_set_date(&clock, 0, (habit_date_t){ .year = 2026, .month = 9, .day = 17 });

    // 睡前写锚点,睡 8 小时后唤醒:墙钟应正好前进 8 小时。
    const uint64_t sleep_rtc = (uint64_t)3600 * USEC;
    const habit_clock_mark_t mark = habit_clock_make_mark(&clock, sleep_rtc);
    assert(mark.magic == HABIT_CLOCK_MARK_MAGIC);
    assert(mark.wall_sec == WALL_SEP17 + 3600);

    const uint64_t wake_rtc = sleep_rtc + (uint64_t)8 * 3600 * USEC;
    habit_clock_t woke;
    assert(habit_clock_boot(&woke, wake_rtc, &mark, true, 0) == HABIT_CLOCK_CERTAIN);
    assert(habit_clock_now_sec(&woke, wake_rtc) == WALL_SEP17 + 9 * 3600);
    // 8 小时不足以跨过 04:00 边界(设定日是当天 04:00 起算)。
    assert(habit_clock_today(&woke, wake_rtc) == DAY);
}

static void test_wall_date_round_trip(void)
{
    const habit_date_t cases[] = {
        { .year = 2000, .month = 1, .day = 1 },
        { .year = 2026, .month = 9, .day = 17 },
        { .year = 2024, .month = 2, .day = 29 },
        { .year = 2099, .month = 12, .day = 31 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const int64_t wall = habit_clock_date_to_wall(cases[i]);
        const habit_date_t back = habit_clock_date_from_wall(wall);
        assert(back.year == cases[i].year);
        assert(back.month == cases[i].month);
        assert(back.day == cases[i].day);

        // 该日边界前 1 秒属于前一个逻辑日 —— 这正是 4 点边界的作用,
        // 必须精确等于"日期减一天",而不是任意其它日期。
        const habit_date_t prev =
            habit_date_from_days(habit_date_to_days(cases[i]) - 1);
        const habit_date_t before = habit_clock_date_from_wall(wall - 1);
        assert(before.year == prev.year);
        assert(before.month == prev.month);
        assert(before.day == prev.day);
    }
}

int main(void)
{
    test_certain_after_deep_sleep();
    test_counter_going_backwards_means_power_loss();
    test_bad_magic_is_ignored();
    test_first_boot_is_unset();
    test_set_date_anchors_at_boundary();
    test_mark_round_trip();
    test_wall_date_round_trip();
    return 0;
}