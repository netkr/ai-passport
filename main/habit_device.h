// main/habit_device.h —— 唯一触碰设备能力的胶水层:RTC 计数、RTC 保留内存、NVS。
//
// 为什么单独一层:habit_clock / habit_store / habit_model 都是纯逻辑,因此能在
// 主机上测;它们需要的"外部世界"只有三样东西 —— 当前 RTC 计数、一块跨深睡
// 保留的内存、一个持久化存储。把这三样收在这一个文件里,纯逻辑层就不必包含
// 任何 esp_* 头文件,主机测试也就不用为 NVS 造桩。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "habit_clock.h"
#include "habit_store.h"

// 初始化 NVS 分区。失败时记录功能不可用,但不影响打卡界面本身的运行。
esp_err_t habit_device_init(void);

// 当前 RTC 计数(微秒)。跨深睡与普通重启连续,掉电归零。
uint64_t habit_device_rtc_us(void);

// RTC 保留内存里的时间锚点。掉电后该内存被清零,magic 自然无效,
// 可直接交给 habit_clock_boot() 判断日期是否可信。
const habit_clock_mark_t *habit_device_mark(void);

// 写入锚点。应在日期变更后、进入深睡前调用。
void habit_device_save_mark(const habit_clock_mark_t *mark);

// 读取持久化记录。found 表示 NVS 里存在一份**解码成功**的记录;
// 数据损坏或版本不认识时返回 ESP_OK 且 found=false,调用方据此走确认流程。
esp_err_t habit_device_load(habit_store_data_t *out, bool *found);

// 写入持久化记录。应在打卡成功、日期变更后调用。
esp_err_t habit_device_save(const habit_records_t *records, int32_t today, int64_t wall_sec);