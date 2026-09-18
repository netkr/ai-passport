#include "habit_date.h"

// 历法换算用 Howard Hinnant 的 civil_from_days / days_from_civil:
// 无循环、无查表、对负天数也正确,且不依赖任何库。这里只做 proleptic
// Gregorian 换算,不做时区处理 —— 墙钟由应用以"本地秒数"的形式持有。

int32_t habit_date_to_days(habit_date_t date)
{
    // 以 3 月为岁首,把闰日固定成一年里的最后一天,免去逐月判断。
    const int32_t y = date.year - (date.month <= 2 ? 1 : 0);
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const int32_t yoe = y - era * 400;                                   // [0, 399]
    const int32_t doy = (153 * (date.month + (date.month > 2 ? -3 : 9)) + 2) / 5
                      + date.day - 1;
    const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
    return era * 146097 + doe - 719468;
}

habit_date_t habit_date_from_days(int32_t days)
{
    const int32_t z = days + 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int32_t doe = z - era * 146097;
    const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t y = yoe + era * 400;
    const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int32_t mp = (5 * doy + 2) / 153;
    const int32_t d = doy - (153 * mp + 2) / 5 + 1;
    const int32_t m = mp + (mp < 10 ? 3 : -9);

    habit_date_t out;
    out.year = y + (m <= 2 ? 1 : 0);
    out.month = (uint8_t)m;
    out.day = (uint8_t)d;
    return out;
}

int32_t habit_date_days_in_month(int32_t year, int month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 0;
    if (month != 2) return days[month - 1];

    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
}

bool habit_date_is_valid(habit_date_t date)
{
    if (date.year < HABIT_YEAR_MIN || date.year > HABIT_YEAR_MAX) return false;
    if (date.month < 1 || date.month > 12) return false;
    if (date.day < 1) return false;
    return date.day <= habit_date_days_in_month(date.year, date.month);
}

int8_t habit_date_weekday(int32_t days)
{
    // 1970-01-01 是周四 = 以周一为 0 的索引 3。
    return (int8_t)(((days % 7) + 7 + 3) % 7);
}

int32_t habit_month_index_of_day(int32_t day)
{
    const habit_date_t date = habit_date_from_days(day);
    return date.year * 12 + ((int32_t)date.month - 1);
}

int32_t habit_month_first_day(int32_t month_index)
{
    // 用向下取整而不是 C 的向零取整:-1 这类序号表示"第 -1 年的 12 月",
    // 向零取整会算出第 0 年的 12 月,整整差一年。
    int32_t year = month_index / 12;
    int32_t month = month_index - year * 12;   // 0..11
    if (month < 0) {
        month += 12;
        year -= 1;
    }
    return habit_date_to_days((habit_date_t){
        .year = year, .month = (uint8_t)(month + 1), .day = 1 });
}

static habit_date_t clamp_day(habit_date_t date)
{
    const int32_t limit = habit_date_days_in_month(date.year, date.month);
    if (date.day > limit) date.day = (uint8_t)limit;
    if (date.day < 1) date.day = 1;
    return date;
}

habit_date_t habit_date_adjust(habit_date_t date, habit_date_field_t field, int delta)
{
    switch (field) {
    case HABIT_FIELD_YEAR: {
        int32_t year = date.year + delta;
        const int32_t span = HABIT_YEAR_MAX - HABIT_YEAR_MIN + 1;
        // 循环而不是夹紧:用户按住上/下键时不会在边界上"卡死"。
        while (year > HABIT_YEAR_MAX) year -= span;
        while (year < HABIT_YEAR_MIN) year += span;
        date.year = year;
        return clamp_day(date);  // 2024-02-29 加一年 → 2025-02-28
    }
    case HABIT_FIELD_MONTH: {
        int32_t month = (int32_t)date.month + delta;
        while (month > 12) month -= 12;
        while (month < 1) month += 12;
        date.month = (uint8_t)month;
        return clamp_day(date);
    }
    case HABIT_FIELD_DAY:
    default: {
        int32_t day = (int32_t)date.day + delta;
        const int32_t limit = habit_date_days_in_month(date.year, date.month);
        while (day > limit) day -= limit;
        while (day < 1) day += limit;
        date.day = (uint8_t)day;
        return date;
    }
    }
}

int32_t habit_date_logical_day(int64_t wall_sec)
{
    // 负数也要向下取整,否则 1970 年之前的墙钟会算错一天。
    const int64_t shifted = wall_sec - HABIT_DAY_BOUNDARY_SEC;
    return (int32_t)(shifted >= 0
                     ? shifted / HABIT_SECS_PER_DAY
                     : (shifted - (HABIT_SECS_PER_DAY - 1)) / HABIT_SECS_PER_DAY);
}