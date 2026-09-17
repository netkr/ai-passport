// main/habit_ui.h —— 打卡应用的像素界面:五个页面与它们之间的导航。
//
// 这一层是**派生应用自己的界面**,不使用基线 demo 的测试菜单、测试页面或
// ui_pixel 视觉外壳(仓库的强制规则),只复用 BSP 驱动与普通 LVGL 控件。
//
// 与设备层的分工:UI 只负责画界面、改纯逻辑状态,以及"告知需要落盘"。
// NVS 读写、RTC 计数、墙钟基准的更新都由应用层在收到 effect 后执行 —— 这样
// UI 里没有一行 esp_* 调用,页面逻辑也不会和存储耦合。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bsp_button.h"
#include "habit_clock.h"
#include "habit_model.h"

// 跨页面共享的状态。
typedef struct {
    habit_records_t records;
    habit_clock_t clock;          // 墙钟基准(纯逻辑,由应用层负责推进)
    int32_t today;                // 当前逻辑日
    habit_clock_state_t clock_state;
    bool storage_ok;              // NVS 是否可用;不可用时仍可打卡,只是不落盘
    int menu_selected;            // 主菜单选中项 0..HABIT_COUNT-1
    int record_selected;          // 记录页选中行 0..HABIT_COUNT-1
    habit_date_t edit_date;       // 日期设置页正在编辑的临时值
    int edit_field;               // 正在编辑的字段(HABIT_FIELD_*)
    int battery_soc;              // 缓存电量;-1 表示不可用
} habit_ui_state_t;

// 按键处理请求应用的副作用。位标志,可能同时要求多项。
typedef enum {
    HABIT_UI_EFFECT_NONE = 0,
    HABIT_UI_EFFECT_RECORDS_CHANGED = 1u << 0,  // 记录有变,应落盘
    HABIT_UI_EFFECT_DATE_CHANGED = 1u << 1,     // 用户确定了日期,应更新墙钟并落盘
    // 操作被拒(例如当天已打卡)。单独成一位置而不是"无副作用",是为了让应用层
    // 能给出负向反馈(低音提示),否则重复打卡在听觉上毫无差别。
    HABIT_UI_EFFECT_REJECTED = 1u << 2,
} habit_ui_effect_t;

// 建立并载入主菜单。进入前 LVGL 必须已就绪。
void habit_ui_show_menu(habit_ui_state_t *state);

// 直接进入日期设置/确认页。first_time 为真时标题用"设置日期",
// 否则用"确认日期"(掉电后沿用最后已知日期,让用户核对)。
void habit_ui_show_date(habit_ui_state_t *state, bool first_time);

// 处理一次按键。由输入任务在**非 LVGL 任务**上下文调用,函数内部自行加锁,
// 返回值表示应用层需要执行的副作用。
habit_ui_effect_t habit_ui_handle_key(habit_ui_state_t *state, bsp_btn_t btn,
                                      bsp_btn_ev_t event);

// 释放当前页面并停止其定时器。退出应用或进深睡前调用。
void habit_ui_release(void);

// 落盘失败提示。应用层在 NVS 写入失败后调用 —— 打卡必须让用户知道是否真的存下了,
// 静默失败会让人误以为记录已保存。
void habit_ui_show_save_failed(void);

// 周期刷新(电量、跨天判断后的界面重建由应用层负责)。由应用层的定时事件调用。
void habit_ui_tick(void);