// components/bsp/include/bsp_button.h
// 三个按键共用一个 ADC 引脚,靠分压电阻区分。电压窗口见 bsp_pins.h。
#pragma once

#include "esp_err.h"
#include <stdbool.h>

// 按键索引。数量用 bsp_pins.h 的 BSP_BTN_COUNT(硬件属性,归引脚表管),
// 这里不再定义尾项计数,避免出现 BSP_BTN_COUNT / BSP_BTN_COUNT_ 两个近似名字。
typedef enum {
    BSP_BTN_UP = 0,
    BSP_BTN_DOWN,
    BSP_BTN_OK,
} bsp_btn_t;

typedef enum {
    BSP_BTN_PRESS = 0,   // 按下瞬间(低延迟,适合游戏类即时响应)
    BSP_BTN_CLICK,       // 单击(按下并抬起)
    BSP_BTN_DOUBLE,      // 双击
    BSP_BTN_LONG,        // 长按
} bsp_btn_ev_t;

// 按键事件回调。运行于 button 组件使用的共享 esp_timer 任务,只能入队或执行同等级
// 的有界操作；勿在其中阻塞、访问 LVGL 或做重活。
typedef void (*bsp_btn_cb_t)(bsp_btn_t btn, bsp_btn_ev_t ev, void *user);

// 成功调用可重复，并更新回调与 user；失败会回滚本次已创建的按键和 ADC 资源。
// ADC 校准失败时返回错误而不是把无效电压解码为按键，修正故障后可重试。
esp_err_t bsp_button_init(bsp_btn_cb_t cb, void *user);

// 读当前 ADC 原始电压(mV)。松开时约 3300;按住某键时约为该键的分压值。
// ★ 换了分压/上拉阻值后,用它测出自己的三档电压,再改 bsp_pins.h 的 BSP_BTN_MV_TABLE。
// 读取失败返回 -1。
int bsp_button_read_mv(void);

// 当前是否有键被按住(读数落在任一电压窗口内)。松开或读取失败返回 false。
// 用途:深睡按键唤醒后,那个唤醒来电的按键在启动时通常还按着 —— 应用需要先等它
// 松开,否则这次按下会被按键组件镜像成一次误操作。
bool bsp_button_any_pressed(void);

// deep sleep 专用：把 BSP_BTN_GPIO(三键共用的 ADC 节点)配成【低电平】唤醒源。
// 板上三键都把这个节点拉低(见 bsp_pins.h 的分压表),所以任意键都能唤醒。
// ESP32-C3 没有 EXT0/EXT1,只有 GPIO 唤醒。
//
// ⚠ 调用方【必须】检查返回值:返回错误说明唤醒源没装上,此时进入 deep sleep 就是
//   "睡了按不醒"。另外 GPIO 唤醒的第一个参数是位掩码而非引脚号,本函数内部已处理。
esp_err_t bsp_button_arm_wakeup(void);
