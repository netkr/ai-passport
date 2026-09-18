// main/habit_clock.h —— 自持墙钟:把 RTC 计数换算成日历时间,并判断日期是否可信。
//
// 为什么需要它:本板没有可用的绝对时间源(未联网、也没有 32.768kHz 晶振)。
// 日期只能由用户设定一次,之后靠 RTC 计数自己往前走。
//
// 本模块是纯逻辑:不读 RTC、不碰 NVS。调用方把"当前 RTC 计数"作为参数传进来,
// 因此整个状态机都能在主机上测试。RTC 内存与 NVS 的读写是应用层的几行琐事。
//
// RTC 计数的语义(已核对 ESP-IDF 源码 esp_hw_support/esp_clk.c):
//   esp_rtc_get_time_us() 用一块保留内存把 RTC 计数累积成连续微秒值,
//   深睡与普通重启后依然连续,只有掉电(首次上电 cal==0)才归零。
//   因此"锚点 + RTC 差值"可以跨深睡推出墙钟,而锚点丢失就意味着掉电。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "habit_date.h"

// 写入 RTC 保留内存的时间锚点。深睡/重启保留,掉电丢失 —— 这正是判断
// "日期是否仍然可信"的依据。
typedef struct {
    uint32_t magic;
    uint32_t reserved;
    uint64_t rtc_us;    // 保存时刻的 RTC 计数
    int64_t wall_sec;   // 保存时刻的墙钟
} habit_clock_mark_t;

#define HABIT_CLOCK_MARK_MAGIC 0x48414249u  /* "HABI" */

typedef enum {
    HABIT_CLOCK_CERTAIN = 0,  // 锚点有效:墙钟由 RTC 差值连续推出
    HABIT_CLOCK_UNCERTAIN,    // 掉电过:只能沿用最后已知日期,必须让用户确认
    HABIT_CLOCK_UNSET,        // 从未设置过日期
} habit_clock_state_t;

typedef struct {
    int64_t base_wall_sec;  // 本次启动时刻的墙钟
    uint64_t base_rtc_us;   // 本次启动时刻的 RTC 计数
} habit_clock_t;

// 建立墙钟基准。mark 可为 NULL;stored_* 是 NVS 里"最后已知"的值(可能不存在)。
habit_clock_state_t habit_clock_boot(habit_clock_t *clock, uint64_t rtc_now,
                                     const habit_clock_mark_t *mark,
                                     bool have_stored, int64_t stored_wall_sec);

// 当前墙钟秒 / 当前逻辑日(已计入 HABIT_DAY_BOUNDARY_SEC)。
int64_t habit_clock_now_sec(const habit_clock_t *clock, uint64_t rtc_now);
int32_t habit_clock_today(const habit_clock_t *clock, uint64_t rtc_now);

// 用户在设置/确认页确定日期后调用:把基准移到"该日 4:00"。
void habit_clock_set_date(habit_clock_t *clock, uint64_t rtc_now, habit_date_t date);

// 生成要写入 RTC 保留内存的锚点(睡前与日期变更后都应更新)。
habit_clock_mark_t habit_clock_make_mark(const habit_clock_t *clock, uint64_t rtc_now);

// 日期 ↔ 墙钟秒。两端都锚定在当日 4:00,与逻辑日边界保持一致。
int64_t habit_clock_date_to_wall(habit_date_t date);
habit_date_t habit_clock_date_from_wall(int64_t wall_sec);