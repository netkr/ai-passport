#include "habit_model.h"

static size_t slot_index(int32_t day)
{
    // 取模后必须落在 [0, SLOTS) —— day 理论上恒为正,但用取模修正可以避免
    // 将来支持更早日期时出现负下标。
    const int32_t wrap = day % HABIT_RECORD_SLOTS;
    return (size_t)((wrap + HABIT_RECORD_SLOTS) % HABIT_RECORD_SLOTS);
}

void habit_records_clear(habit_records_t *records)
{
    for (size_t i = 0; i < HABIT_RECORD_SLOTS; i++) {
        records->slots[i].day = HABIT_SLOT_EMPTY;
        records->slots[i].mask = 0;
    }
}

uint8_t habit_records_mask(const habit_records_t *records, int32_t day)
{
    const habit_slot_t *slot = &records->slots[slot_index(day)];
    // 日期不匹配即视为无记录:槽里可能是被绕回覆盖的陈旧数据。
    return slot->day == day ? slot->mask : 0;
}

uint32_t habit_records_day_count(const habit_records_t *records, int32_t day)
{
    uint8_t mask = habit_records_mask(records, day);
    // 逐位累加而不是 __builtin_popcount:纯逻辑层要能在任意主机编译器上编译,
    // 6 位的循环开销在这里毫无意义。
    uint32_t count = 0;
    while (mask != 0) {
        count += mask & 1u;
        mask = (uint8_t)(mask >> 1);
    }
    return count;
}

habit_checkin_result_t habit_records_check_in(habit_records_t *records, int32_t day,
                                              habit_id_t habit)
{
    habit_slot_t *slot = &records->slots[slot_index(day)];
    if (slot->day != day) {
        slot->day = day;
        slot->mask = 0;
    }

    const uint8_t bit = (uint8_t)(1u << (unsigned)habit);
    if ((slot->mask & bit) != 0) return HABIT_CHECKIN_ALREADY;

    slot->mask = (uint8_t)(slot->mask | bit);
    return HABIT_CHECKIN_OK;
}

bool habit_records_undo(habit_records_t *records, int32_t day, habit_id_t habit)
{
    habit_slot_t *slot = &records->slots[slot_index(day)];
    // 槽里存的不是这一天,说明该日根本没有记录 —— 没有可撤销的动作。
    if (slot->day != day) return false;

    const uint8_t bit = (uint8_t)(1u << (unsigned)habit);
    if ((slot->mask & bit) == 0) return false;

    // 清位后即使 mask 变 0 也保留 slot->day:读取端以 mask 判断有无记录,
    // 留着日期可以避免"这一天从没来过"与"来过后被撤销"在数据上混为一谈。
    slot->mask = (uint8_t)(slot->mask & (uint8_t)~bit);
    return true;
}

uint32_t habit_records_streak(const habit_records_t *records, int32_t today,
                              habit_id_t habit)
{
    const uint8_t bit = (uint8_t)(1u << (unsigned)habit);
    int32_t day = today;
    if ((habit_records_mask(records, day) & bit) == 0) {
        day--;  // 今天还没打卡:从昨天开始数,避免当天未结束就判定中断。
    }

    uint32_t count = 0;
    while (count < HABIT_RECORD_SLOTS && (habit_records_mask(records, day) & bit) != 0) {
        count++;
        day--;
    }
    return count;
}

bool habit_records_has_any(const habit_records_t *records)
{
    for (size_t i = 0; i < HABIT_RECORD_SLOTS; i++) {
        if (records->slots[i].mask != 0) return true;
    }
    return false;
}