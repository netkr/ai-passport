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

static void test_every_habit_has_its_own_bit(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    // 6 项各自对应一个独立的 bit。位序是存储格式的一部分:新增项必须追加在
    // 末尾,已存的老记录(低 3 位)才不会被解释成别的项目。
    for (int i = 0; i < HABIT_COUNT; i++) {
        assert(habit_records_check_in(&records, DAY, (habit_id_t)i) == HABIT_CHECKIN_OK);
        assert(habit_records_mask(&records, DAY) == (uint8_t)((1u << (i + 1)) - 1u));
    }
    assert(habit_records_mask(&records, DAY) == (uint8_t)((1u << HABIT_COUNT) - 1u));

    // 全部打出后,每一项都应被识别为重复。
    for (int i = 0; i < HABIT_COUNT; i++) {
        assert(habit_records_check_in(&records, DAY, (habit_id_t)i) == HABIT_CHECKIN_ALREADY);
    }
}

static void test_day_count(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    assert(habit_records_day_count(&records, DAY) == 0);
    // 没有记录的日期也必须是 0,而不是靠 mask 恰好为 0 蒙对。
    assert(habit_records_day_count(&records, DAY + 5) == 0);

    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_day_count(&records, DAY) == 1);
    // 重复打卡不增加计数。
    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_ALREADY);
    assert(habit_records_day_count(&records, DAY) == 1);

    assert(habit_records_check_in(&records, DAY, HABIT_EARLY_RISE) == HABIT_CHECKIN_OK);
    assert(habit_records_check_in(&records, DAY, HABIT_READ) == HABIT_CHECKIN_OK);
    assert(habit_records_day_count(&records, DAY) == 3);

    // 上限就是项目总数。
    for (int i = 0; i < HABIT_COUNT; i++) {
        (void)habit_records_check_in(&records, DAY, (habit_id_t)i);
    }
    assert(habit_records_day_count(&records, DAY) == HABIT_COUNT);
}

static void test_undo(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    // 没有记录的日期:无可撤销,且不得凭空创建槽位。
    assert(!habit_records_undo(&records, DAY, HABIT_SLEEP));
    assert(!habit_records_has_any(&records));
    assert(habit_records_mask(&records, DAY) == 0);

    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_check_in(&records, DAY, HABIT_READ) == HABIT_CHECKIN_OK);
    assert(habit_records_mask(&records, DAY) == 0x11);  // bit0 与 bit4

    // 撤销只清自己那一位。
    assert(habit_records_undo(&records, DAY, HABIT_SLEEP));
    assert(habit_records_mask(&records, DAY) == 0x10);
    assert(habit_records_day_count(&records, DAY) == 1);

    // 幂等:再撤一次返回 false,且状态不变。
    assert(!habit_records_undo(&records, DAY, HABIT_SLEEP));
    assert(habit_records_mask(&records, DAY) == 0x10);

    // 撤销后可以重新打卡(撤销不是"永久封锁")。
    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_mask(&records, DAY) == 0x11);

    // 别的日期不受影响。
    assert(habit_records_check_in(&records, DAY + 1, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_undo(&records, DAY, HABIT_SLEEP));
    assert(habit_records_mask(&records, DAY + 1) == 0x01);

    // 撤到最后一位时槽位仍在(has_any 由 mask 决定,不会残留虚假记录)。
    assert(habit_records_undo(&records, DAY, HABIT_READ));
    assert(habit_records_mask(&records, DAY) == 0);
    assert(habit_records_day_count(&records, DAY) == 0);
    // 连续天数按日往回数,所以撤销当天会把 DAY 这一天从连续段里摘掉。
    assert(habit_records_streak(&records, DAY + 1, HABIT_SLEEP) == 1);
    assert(habit_records_streak(&records, DAY, HABIT_SLEEP) == 0);
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
    test_every_habit_has_its_own_bit();
    test_day_count();
    test_undo();
    test_ring_wrap_invalidates_stale_slot();
    test_streak();
    test_streak_is_capped_by_slots();
    return 0;
}