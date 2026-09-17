// main/habit_app.c —— 把设备、纯逻辑层和界面接在一起。
//
// 并发模型(与基线 demo 相同的思路,但收得更紧):
//   按键回调(共享 esp_timer 任务)和周期回调都只做一件事 —— 入队。
//   界面操作、跨天判断、NVS 写入全部在唯一的 habit_event 任务里完成。
//   因此不存在"两个任务同时碰 LVGL 或同一份记录"的情况。
//
// 分工:
//   habit_ui 只画界面并告知需要落盘(返回 effect);
//   本文件负责 RTC 读数、NVS 读写、墙钟基准推进。
#include "habit_app.h"

#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "habit_audio.h"
#include "habit_clock.h"
#include "habit_device.h"
#include "habit_date.h"
#include "habit_store.h"
#include "habit_ui.h"

static const char *TAG = "habit_app";

#define INPUT_QUEUE_DEPTH 8
#define EVENT_TASK_STACK 4096
#define EVENT_TASK_PRIO 5

// 周期事件的用途:电量刷新与跨天判断。5 秒足够 —— 跨天判定本身还有 4 点边界
// 的吸收,不需要秒级精度;而屏幕只在唤醒且有界面时才有意义。
#define TICK_INTERVAL_US (5 * 1000 * 1000)

// 首次开机时日期编辑页的起点。只要求落在合法范围内,用户再用上下键调到当天。
// 刻意不解析编译日期(__DATE__):那会让行为随构建时间变化,不可复现也不值得。
#define DEFAULT_YEAR 2026
#define DEFAULT_MONTH 1
#define DEFAULT_DAY 1

typedef enum {
    EVENT_KEY = 0,
    EVENT_TICK,
} event_kind_t;

typedef struct {
    event_kind_t kind;
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} app_event_t;

static habit_ui_state_t s_state;
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static esp_timer_handle_t s_tick_timer;
static bool s_ready;

static bool post(const app_event_t *event)
{
    if (!s_ready || s_queue == NULL) return false;
    return xQueueSend(s_queue, event, 0) == pdTRUE;
}

// 按键回调运行在 button 组件使用的共享 esp_timer 任务:只入队并立刻返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t event, void *user)
{
    (void)user;
    const app_event_t queued = { .kind = EVENT_KEY, .btn = btn, .event = event };
    (void)post(&queued);
}

static void on_tick(void *arg)
{
    (void)arg;
    const app_event_t queued = { .kind = EVENT_TICK };
    (void)post(&queued);
}

static int64_t now_sec(void)
{
    return habit_clock_now_sec(&s_state.clock, habit_device_rtc_us());
}

// 更新 RTC 保留内存里的时间锚点。锚点在深睡/重启后仍然有效,是"日期是否可信"
// 的唯一依据,所以在日期变更、跨天、以及每次启动时都刷新。
static void refresh_mark(void)
{
    const habit_clock_mark_t mark =
        habit_clock_make_mark(&s_state.clock, habit_device_rtc_us());
    habit_device_save_mark(&mark);
}

static void save_records(void)
{
    if (!s_state.storage_ok) return;
    const esp_err_t err = habit_device_save(&s_state.records, s_state.today, now_sec());
    if (err != ESP_OK) {
        // 不静默:打卡没存下必须让用户看见和听见,否则重启后记录凭空消失。
        ESP_LOGE(TAG, "打卡记录落盘失败: %s", esp_err_to_name(err));
        habit_audio_play(HABIT_SOUND_REJECTED);
        habit_ui_show_save_failed();
    }
}

static void apply_effect(habit_ui_effect_t effect)
{
    // 听觉反馈由应用层决定,界面只报告"发生了什么"。
    if ((effect & HABIT_UI_EFFECT_REJECTED) != 0) habit_audio_play(HABIT_SOUND_REJECTED);

    if ((effect & HABIT_UI_EFFECT_DATE_CHANGED) != 0) {
        const uint64_t rtc = habit_device_rtc_us();
        habit_clock_set_date(&s_state.clock, rtc, s_state.edit_date);
        s_state.today = habit_clock_today(&s_state.clock, rtc);
        s_state.clock_state = HABIT_CLOCK_CERTAIN;
        refresh_mark();  // 立即建立锚点,缩短下次掉电后需要人工确认的窗口
        save_records();  // 日期基准变了,把墙钟一并写进持久数据
    }
    if ((effect & HABIT_UI_EFFECT_RECORDS_CHANGED) != 0) {
        save_records();
        habit_audio_play(HABIT_SOUND_OK);
    }
}

// 跨天:记录按逻辑日索引,所以只需推进 today 并重建界面(日期栏与打卡状态
// 都基于新的一天)。
static void maybe_roll_day(void)
{
    const uint64_t rtc = habit_device_rtc_us();
    const int32_t today = habit_clock_today(&s_state.clock, rtc);
    if (today == s_state.today) return;

    ESP_LOGI(TAG, "跨天: %d -> %d", (int)s_state.today, (int)today);
    s_state.today = today;
    refresh_mark();
    habit_ui_show_menu(&s_state);
}

static void event_task(void *arg)
{
    (void)arg;
    app_event_t event;
    for (;;) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.kind == EVENT_TICK) {
            // 电量在这里读:不持 LVGL 锁。CW2017 的 I2C 事务超时可达 100ms,
            // 放在渲染锁里会卡住刷屏。
            s_state.battery_soc = bsp_battery_soc();
            maybe_roll_day();
            habit_ui_tick();
            continue;
        }
        apply_effect(habit_ui_handle_key(&s_state, event.btn, event.event));
    }
}

void habit_app_run(void)
{
    const esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "唤醒原因: %d", (int)wakeup);
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是本应用唯一的界面载体,失败就没有可用界面 —— 如实报错后退出,
    // 不做串口降级(那会让这一层复杂一倍,而且用户根本看不到)。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,应用无法继续;检查 SPI 接线");
        return;
    }
    bsp_display_backlight(100);

    // 提示音是软依赖:失败只是没声音,界面与记录照常。
    (void)habit_audio_init();

    // 电量计是软依赖:不在或读取失败时 bsp_battery_soc() 返回 -1,
    // 界面会隐藏电量项而不是画一个假数字。
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用:界面将不显示电量");
    }
    s_state.battery_soc = bsp_battery_soc();

    s_state.storage_ok = (habit_device_init() == ESP_OK);
    habit_records_clear(&s_state.records);

    // 持久数据:记录与"最后已知日期"。读不出来就按没有处理。
    bool have_stored = false;
    habit_store_data_t stored;
    memset(&stored, 0, sizeof(stored));
    if (s_state.storage_ok) {
        const esp_err_t err = habit_device_load(&stored, &have_stored);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "读取持久数据失败: %s", esp_err_to_name(err));
            have_stored = false;
        }
    }
    if (have_stored) s_state.records = stored.records;

    // 墙钟:锚点有效则由 RTC 差值连续推出;掉电过就只能沿用最后已知日期,
    // 并请用户在日期页确认。
    const uint64_t rtc = habit_device_rtc_us();
    s_state.clock_state = habit_clock_boot(&s_state.clock, rtc, habit_device_mark(),
                                          have_stored, stored.wall_sec);
    s_state.today = habit_clock_today(&s_state.clock, rtc);
    s_state.menu_selected = 0;
    s_state.record_selected = 0;

    s_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(app_event_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "输入队列创建失败,应用无法继续");
        return;
    }
    if (xTaskCreate(event_task, "habit_event", EVENT_TASK_STACK, NULL,
                    EVENT_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "输入任务创建失败,应用无法继续");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败:界面将无法操作");
    }

    const esp_timer_create_args_t tick_args = { .callback = on_tick, .name = "habit_tick" };
    if (esp_timer_create(&tick_args, &s_tick_timer) == ESP_OK) {
        esp_timer_start_periodic(s_tick_timer, TICK_INTERVAL_US);
    } else {
        ESP_LOGW(TAG, "周期定时器创建失败:电量与跨天检查不可用");
    }

    s_state.edit_date = have_stored
        ? habit_date_from_days(s_state.today)
        : (habit_date_t){ .year = DEFAULT_YEAR, .month = DEFAULT_MONTH,
                          .day = DEFAULT_DAY };

    s_ready = true;
    if (s_state.clock_state == HABIT_CLOCK_CERTAIN) {
        refresh_mark();
        habit_ui_show_menu(&s_state);
    } else {
        // 首次开机或掉电过:日期不可信,必须先设置或确认。
        habit_ui_show_date(&s_state, s_state.clock_state == HABIT_CLOCK_UNSET);
    }

    ESP_LOGI(TAG, "就绪: 日期状态=%d 持久化=%d", (int)s_state.clock_state,
             (int)s_state.storage_ok);
}