// main/habit_app.c —— 把设备、纯逻辑层和界面接在一起。
//
// 并发模型(与基线 demo 相同的思路,但收得更紧):
//   按键回调(共享 esp_timer 任务)和周期回调都只做一件事 —— 入队。
//   界面操作、跨天判断、NVS 写入全部在唯一的 habit_event 任务里完成。
//   因此不存在"两个任务同时碰 LVGL 或同一份记录"的情况。
//
// 分工:
//   habit_ui 只画界面并告知需要落盘(返回 effect);
//   本文件负责 RTC 读数、NVS 读写、墙钟基准推进,以及省电(熄屏/深睡)。
#include "habit_app.h"

#include <string.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
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

// 省电两级:先关背光,再深睡。深睡会重启应用,所以第一级保留 MCU 运行,
// 让"抬手看一眼又放下"不触发一次重启。两者都从最后一次按键重新计时。
#define SCREEN_OFF_TIMEOUT_US (15 * 1000 * 1000)
#define DEEP_SLEEP_TIMEOUT_US (30 * 1000 * 1000)

// 深睡前等待 LVGL 刷屏停止的上限(与基线 demo 同值)。
#define LVGL_STOP_TIMEOUT_MS 1000

// 首次开机时日期编辑页的起点。只要求落在合法范围内,用户再用上下键调到当天。
// 刻意不解析编译日期(__DATE__):那会让行为随构建时间变化,不可复现也不值得。
#define DEFAULT_YEAR 2026
#define DEFAULT_MONTH 1
#define DEFAULT_DAY 1

typedef enum {
    EVENT_KEY = 0,
    EVENT_TICK,
    EVENT_SCREEN_OFF,
    EVENT_DEEP_SLEEP,
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
static esp_timer_handle_t s_screen_off_timer;
static esp_timer_handle_t s_deep_sleep_timer;
static bool s_ready;
static bool s_screen_off;
// 点亮屏幕的那一次按下的"后续事件"要丢掉:界面把按下镜像成 CLICK/LONG,
// 否则叫醒屏幕的空按会顺手改掉选中项。
static bool s_swallow_key_up;
// 深睡唤醒后,唤醒来电的那个键在启动时通常还按着,先等它松开再接受操作。
static bool s_hold_wait;

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

// 两个空闲定时器共用这个回调:kind 由创建时传入的 arg 决定。
static void on_idle_timeout(void *arg)
{
    const app_event_t queued = { .kind = (event_kind_t)(intptr_t)arg };
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

// ---------------------------------------------------------------------------
// 省电:熄屏与深睡
//
// 只有按键能重新点亮屏幕,所以"熄屏"状态下的第一次按下只用来唤醒,不当成界面
// 操作 —— 界面把按下镜像成 CLICK/LONG,转发过去就会顺手改掉选中项。
// ---------------------------------------------------------------------------
static void restart_timer(esp_timer_handle_t timer, uint64_t us)
{
    if (timer == NULL) return;
    // 正在计时时 stop 成功、未启动时返回 INVALID_STATE;两种情况都要重新起算,
    // 所以返回值只做忽略处理。
    (void)esp_timer_stop(timer);
    (void)esp_timer_start_once(timer, us);
}

// 每次按键都重新起算熄屏与深睡两级的时限。
static void arm_idle_timers(void)
{
    restart_timer(s_screen_off_timer, SCREEN_OFF_TIMEOUT_US);
    restart_timer(s_deep_sleep_timer, DEEP_SLEEP_TIMEOUT_US);
}

static void wake_screen(void)
{
    if (!s_screen_off) return;
    s_screen_off = false;
    bsp_display_backlight(100);
}

static void screen_off(void)
{
    if (s_screen_off) return;
    s_screen_off = true;
    // 只关背光:面板与界面都保持原样,下次按键亮屏后立刻是离开时的画面
    // (面板真正的 Sleep In 在深睡前做,由 bsp_display_prepare_deep_sleep 负责)。
    bsp_display_backlight(0);
}

// 深睡入口。返回意味着"本次休眠被放弃",调用方可以继续正常跑。
static void enter_deep_sleep(void)
{
    // 1. 先装唤醒源。这一步失败绝不能往下走:跑完终端序列后已无退路,而唤醒源
    //    没装上就是"睡了按不醒"(ESP32-C3 只有 GPIO 唤醒,没有 EXT0/EXT1)。
    const esp_err_t wake_err = bsp_button_arm_wakeup();
    if (wake_err != ESP_OK) {
        ESP_LOGE(TAG, "按键唤醒源配置失败(%s):放弃本次休眠", esp_err_to_name(wake_err));
        wake_screen();
        arm_idle_timers();
        return;
    }

    // 2. 让提示音任务停下。bsp_audio_sleep() 与 bsp_audio_write() 并发会破坏
    //    ES8311 的挂起序列,而播放任务在写 PCM 时的等待虽有界但并不为零。
    if (habit_audio_suspend() != ESP_OK) {
        ESP_LOGE(TAG, "提示音任务未能停止:放弃本次休眠");
        wake_screen();
        arm_idle_timers();
        return;
    }

    // 3. 到这里不再有可回退的失败点:冻结输入并停掉会再访问外设的定时器
    //    (周期事件会读 I2C 电量)。
    s_ready = false;
    (void)esp_timer_stop(s_tick_timer);
    (void)esp_timer_stop(s_screen_off_timer);
    (void)esp_timer_stop(s_deep_sleep_timer);

    // 这行通常抓不到:进深睡时 USB Serial/JTAG 随外设一起断电,最后一段输出会跟着
    // 消失。留着是为了排查"唤醒源配好了但没睡成"这类情况,别把它当成必然可见的证据。
    ESP_LOGI(TAG, "进入 deep sleep:等待任意键唤醒");

    // 4. 把"最后已知日期"刷进 NVS:深睡期间若掉电,启动只能靠它给出大致日期,
    //    而这次深睡可能很长,不该让用户看到几天前的日期。
    save_records();

    // 5. 终端关机序列,顺序不可调换 —— 每一步之后对应外设就不该再被访问:
    //    CW2017 与 ES8311 的最后一笔 I2C → I2S 引脚 → 共享 I2C → 面板。
    //    单步失败只记日志:中断序列会让后面几步完全不做,反而更糟。
    if (bsp_battery_sleep() != ESP_OK) ESP_LOGW(TAG, "CW2017 挂起未确认:继续关机");
    if (bsp_audio_sleep() != ESP_OK) ESP_LOGW(TAG, "ES8311 挂起未确认:继续关机");
    if (bsp_audio_prepare_deep_sleep() != ESP_OK) ESP_LOGW(TAG, "I2S 引脚未释放");
    if (bsp_i2c_prepare_deep_sleep() != ESP_OK) ESP_LOGW(TAG, "共享 I2C 引脚未释放");

    // 面板关闭前先阻止 LVGL 刷屏:持锁到入睡,中途不释放。
    if (!bsp_lvgl_lock(LVGL_STOP_TIMEOUT_MS)) {
        // I2S/I2C 已处于不可恢复的终态,不能再假装没事继续跑,重启恢复外设。
        ESP_LOGE(TAG, "deep sleep 前无法阻止 LVGL 刷屏,改为重启恢复外设");
        esp_restart();
    }
    if (bsp_display_prepare_deep_sleep() != ESP_OK) ESP_LOGW(TAG, "面板未进入 Sleep In");

    esp_deep_sleep_start();
    // 走到这里说明没睡成。外设已被释放,重启而不是硬撑。
    ESP_LOGE(TAG, "esp_deep_sleep_start 意外返回,重启恢复外设");
    esp_restart();
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
        if (event.kind == EVENT_SCREEN_OFF) {
            screen_off();
            continue;
        }
        if (event.kind == EVENT_DEEP_SLEEP) {
            // 失败(唤醒源或提示音任务没准备好)会返回,定时器已重新起算。
            enter_deep_sleep();
            continue;
        }

        // 深睡唤醒:先等唤醒来电的那次按下松开。这次按下与后续的 CLICK/LONG
        // 都不是新操作,放行会让界面"莫名其妙"跳一格。
        if (s_hold_wait) {
            if (bsp_button_any_pressed()) continue;
            s_hold_wait = false;
            continue;
        }

        // 按下瞬间:界面不用 PRESS,这里只负责"按下即亮屏"。
        if (event.event == BSP_BTN_PRESS) {
            const bool woke = s_screen_off;
            if (woke) wake_screen();
            s_swallow_key_up = woke;
            arm_idle_timers();
            continue;
        }
        if (s_swallow_key_up) continue;
        arm_idle_timers();
        apply_effect(habit_ui_handle_key(&s_state, event.btn, event.event));
    }
}

void habit_app_run(void)
{
    const esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();

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
    } else {
        // 由按键唤醒时,那个键在启动这一刻通常还按着 —— 先等它松开再接受操作。
        // 只采样一次:此刻没按住说明唤醒的那次按下已经结束,无需再等待。
        s_hold_wait = (wakeup == ESP_SLEEP_WAKEUP_GPIO) && bsp_button_any_pressed();
    }

    const esp_timer_create_args_t tick_args = { .callback = on_tick, .name = "habit_tick" };
    if (esp_timer_create(&tick_args, &s_tick_timer) == ESP_OK) {
        esp_timer_start_periodic(s_tick_timer, TICK_INTERVAL_US);
    } else {
        ESP_LOGW(TAG, "周期定时器创建失败:电量与跨天检查不可用");
    }

    // 两个空闲定时器各自独立:任一创建失败只损失对应的那一级省电。
    const esp_timer_create_args_t screen_args = {
        .callback = on_idle_timeout,
        .arg = (void *)(intptr_t)EVENT_SCREEN_OFF,
        .name = "habit_blank",
    };
    if (esp_timer_create(&screen_args, &s_screen_off_timer) != ESP_OK) {
        ESP_LOGW(TAG, "熄屏定时器创建失败:屏幕将一直亮到深睡");
        s_screen_off_timer = NULL;
    }
    const esp_timer_create_args_t sleep_args = {
        .callback = on_idle_timeout,
        .arg = (void *)(intptr_t)EVENT_DEEP_SLEEP,
        .name = "habit_sleep",
    };
    if (esp_timer_create(&sleep_args, &s_deep_sleep_timer) != ESP_OK) {
        ESP_LOGW(TAG, "深睡定时器创建失败:不会自动休眠");
        s_deep_sleep_timer = NULL;
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
    // 空闲计时从启动就开始:日期确认页放着不管同样会熄屏、进深睡。
    arm_idle_timers();

    // 唤醒原因打在这里而不是开机第一行:USB Serial/JTAG 在枚举完成前的那一小段
    // 输出会被丢掉,而这行是"按键深睡唤醒真的生效"的唯一现场证据,必须落在能抓到
    // 的位置。ESP_SLEEP_WAKEUP_GPIO(=7) 即按键唤醒;0 表示上电或普通复位。
    ESP_LOGI(TAG, "就绪: 日期状态=%d 持久化=%d 唤醒原因=%d", (int)s_state.clock_state,
             (int)s_state.storage_ok, (int)wakeup);
}