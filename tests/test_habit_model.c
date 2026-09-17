#include <assert.h>
#include <stdint.h>

#include "habit_model.h"

// 2026-09-17 的日序号(与 test_habit_date.c 的参照值一致)。
#define DAY 20713

static void test_empty(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    assert(!habit_records_has_any(&records));
    assert(habit_records_mask(&records, DAY) == 0);
    assert(habit_records_streak(&records, DAY, HABIT_SLEEP) == 0);
}

static void test_check_in_and_duplicate(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_mask(&records, DAY) == 0x01);
    assert(habit_records_has_any(&records));

    // 重复打卡必须被识别,不能静默重复计数或改动其它位。
    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_ALREADY);
    assert(habit_records_mask(&records, DAY) == 0x01);

    // 三项互不干扰,位序与 habit_id_t 一致。
    assert(habit_records_check_in(&records, DAY, HABIT_QUIT) == HABIT_CHECKIN_OK);
    assert(habit_records_mask(&records, DAY) == 0x05);
    assert(habit_records_check_in(&records, DAY, HABIT_EXERCISE) == HABIT_CHECKIN_OK);
    assert(habit_records_mask(&records, DAY) == 0x07);

    // 别的日期不受影响。
    assert(habit_records_mask(&records, DAY + 1) == 0);
    assert(habit_records_mask(&records, DAY - 1) == 0);
}

static void test_ring_wrap_invalidates_stale_slot(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    // HABIT_RECORD_SLOTS 天后落回同一个槽:DATE 与 DATE+90 同余。
    const int32_t later = DAY + HABIT_RECORD_SLOTS;
    assert(DAY % HABIT_RECORD_SLOTS == later % HABIT_RECORD_SLOTS);

    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_mask(&records, DAY) == 0x01);

    assert(habit_records_check_in(&records, later, HABIT_QUIT) == HABIT_CHECKIN_OK);

    // 关键行为:绕回后旧日期必须读成"无记录",而不是读到新数据。
    assert(habit_records_mask(&records, later) == 0x04);  // HABIT_QUIT 是第三个枚举值
    assert(habit_records_mask(&records, DAY) == 0x00);
}

static void test_streak(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    // 无记录。
    assert(habit_records_streak(&records, DAY, HABIT_SLEEP) == 0);

    // 连续三天(含今天)。
    for (int32_t d = DAY - 2; d <= DAY; d++) {
        assert(habit_records_check_in(&records, d, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    }
    assert(habit_records_streak(&records, DAY, HABIT_SLEEP) == 3);

    // 今天尚未打卡时,连续天数不应提前归零,而是延续到昨天。
    assert(habit_records_streak(&records, DAY + 1, HABIT_SLEEP) == 3);
    // 隔了一整天仍未打卡,才算中断。
    assert(habit_records_streak(&records, DAY + 2, HABIT_SLEEP) == 0);

    // 断档:DAY 与 DAY-1 之间缺口,只算一天。
    habit_records_clear(&records);
    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_check_in(&records, DAY - 2, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_streak(&records, DAY, HABIT_SLEEP) == 1);

    // 其它类别不共享连续天数。
    assert(habit_records_streak(&records, DAY, HABIT_QUIT) == 0);
}

static void test_streak_is_capped_by_slots(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    // 填满全部槽位:连续天数应正好等于槽数,且不会越界读到同余的旧槽。
    for (int32_t d = DAY - (HABIT_RECORD_SLOTS - 1); d <= DAY; d++) {
        assert(habit_records_check_in(&records, d, HABIT_EXERCISE) == HABIT_CHECKIN_OK);
    }
    assert(habit_records_streak(&records, DAY, HABIT_EXERCISE) == HABIT_RECORD_SLOTS);
}

int main(void)
{
    test_empty();
    test_check_in_and_duplicate();
    test_ring_wrap_invalidates_stale_slot();
    test_streak();
    test_streak_is_capped_by_slots();
    return 0;
}