// main/habit_model.h —— 打卡记录的状态模型。纯 C,不含 ESP-IDF / LVGL,
// 因此全部行为都能在主机上用 tests/test_habit_model.c 覆盖。
//
// 记录采用"按日定位的环形槽":
//   槽位 = day % HABIT_RECORD_SLOTS,每个槽自带它记录的日期。
//   写入时若槽里存的日期不是目标日期,说明那是被绕回覆盖的旧数据,直接覆盖。
//   读取时日期不匹配就当作"无记录"—— 不需要额外的失效逻辑,也不会读到
//   90 天前同余的陈旧数据。
// 设计代价:最多只能保留 HABIT_RECORD_SLOTS 天,连续天数也因此封顶。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "habit_date.h"

typedef enum {
    HABIT_SLEEP = 0,   // 早睡
    HABIT_EXERCISE,    // 锻炼
    HABIT_QUIT,        // 戒烟
    HABIT_COUNT,
} habit_id_t;

#define HABIT_RECORD_SLOTS 90
#define HABIT_SLOT_EMPTY INT32_MIN

typedef struct {
    int32_t day;
    uint8_t mask;  // bit(habit_id_t)
} habit_slot_t;

typedef struct {
    habit_slot_t slots[HABIT_RECORD_SLOTS];
} habit_records_t;

typedef enum {
    HABIT_CHECKIN_OK = 0,
    HABIT_CHECKIN_ALREADY,  // 当天该项已打过卡,不重复记录
} habit_checkin_result_t;

void habit_records_clear(habit_records_t *records);

// 指定日期的打卡位图;无记录返回 0。
uint8_t habit_records_mask(const habit_records_t *records, int32_t day);

habit_checkin_result_t habit_records_check_in(habit_records_t *records, int32_t day,
                                              habit_id_t habit);

// 连续打卡天数(含今天)。今天尚未打卡时从昨天往回数 —— 当天还没结束,
// 不应提前判定为中断。
uint32_t habit_records_streak(const habit_records_t *records, int32_t today,
                              habit_id_t habit);

// 是否有任何记录。用于记录页显示"暂无记录"。
bool habit_records_has_any(const habit_records_t *records);