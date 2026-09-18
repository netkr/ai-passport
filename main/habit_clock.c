#include "habit_clock.h"

#define HABIT_US_PER_SEC 1000000

static bool mark_is_usable(const habit_clock_mark_t *mark, uint64_t rtc_now)
{
    if (mark == NULL || mark->magic != HABIT_CLOCK_MARK_MAGIC) return false;
    // RTC 计数只会单调增长;倒退回更小的值说明掉电后重新计数,锚点已失效。
    return rtc_now >= mark->rtc_us;
}

habit_clock_state_t habit_clock_boot(habit_clock_t *clock, uint64_t rtc_now,
                                     const habit_clock_mark_t *mark,
                                     bool have_stored, int64_t stored_wall_sec)
{
    clock->base_rtc_us = rtc_now;

    if (mark_is_usable(mark, rtc_now)) {
        // 关键路径:锚点里的墙钟 + 这段时间流逝的秒数。深睡期间 CPU 停了,
        // 但 RTC 计数没停,所以这个差值就是真实经过的时间。
        const uint64_t elapsed_us = rtc_now - mark->rtc_us;
        clock->base_wall_sec = mark->wall_sec + (int64_t)(elapsed_us / HABIT_US_PER_SEC);
        return HABIT_CLOCK_CERTAIN;
    }

    // 掉电后无法得知"离线了多久",只能沿用最后已知日期并请用户确认。
    clock->base_wall_sec = have_stored ? stored_wall_sec : 0;
    return have_stored ? HABIT_CLOCK_UNCERTAIN : HABIT_CLOCK_UNSET;
}

int64_t habit_clock_now_sec(const habit_clock_t *clock, uint64_t rtc_now)
{
    // 用 RTC 计数而不是 esp_timer:两者在本应用里等价,但 RTC 计数与锚点同一
    // 时间轴,少一次换算,也避免把"锚点用 RTC、运行时用 esp_timer"这种混用
    // 变成日后的隐患。
    return clock->base_wall_sec + (int64_t)((rtc_now - clock->base_rtc_us) / HABIT_US_PER_SEC);
}

int32_t habit_clock_today(const habit_clock_t *clock, uint64_t rtc_now)
{
    return habit_date_logical_day(habit_clock_now_sec(clock, rtc_now));
}

void habit_clock_set_date(habit_clock_t *clock, uint64_t rtc_now, habit_date_t date)
{
    clock->base_rtc_us = rtc_now;
    clock->base_wall_sec = habit_clock_date_to_wall(date);
}

habit_clock_mark_t habit_clock_make_mark(const habit_clock_t *clock, uint64_t rtc_now)
{
    habit_clock_mark_t mark;
    mark.magic = HABIT_CLOCK_MARK_MAGIC;
    mark.reserved = 0;
    mark.rtc_us = rtc_now;
    mark.wall_sec = habit_clock_now_sec(clock, rtc_now);
    return mark;
}

int64_t habit_clock_date_to_wall(habit_date_t date)
{
    // 锚定在当日 4:00:这样"日期"与"逻辑日"是同一个概念的两面,
    // 用户设定的 9 月 17 日从 17 日 04:00 开始,到 18 日 03:59 结束。
    return (int64_t)habit_date_to_days(date) * HABIT_SECS_PER_DAY
         + HABIT_DAY_BOUNDARY_SEC;
}

habit_date_t habit_clock_date_from_wall(int64_t wall_sec)
{
    return habit_date_from_days(habit_date_logical_day(wall_sec));
}