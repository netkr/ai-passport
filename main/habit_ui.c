// main/habit_ui.c —— 五个像素页面:主菜单 / 打卡确认 / 结果 / 记录 / 日期设置。
//
// 视觉语言刻意与基线 demo 不同(基线是"云朵+草地+厚边框卡片")。这里用:
//   · 纯平色块 + 2px 直角描边(不用圆角、不用投影)
//   · 10~18px 的像素方块作为状态指示(已打卡=实心绿、未打卡=空心描边)
//   · 12px 正文 / 24px 标题的点阵中文字体(24px 是 12px 的精确 2 倍)
// 屏幕四角被 BSP 裁成 30px 圆角遮罩,所以贴边内容必须内缩(见 k_margin_* )。
#include "habit_ui.h"

#include "bsp_display.h"  // bsp_lvgl_lock / bsp_lvgl_unlock
#include "esp_log.h"
#include "habit_date.h"
#include "habit_strings.h"
#include "lvgl.h"

LV_FONT_DECLARE(app_font_12);
LV_FONT_DECLARE(app_font_24);

static const char *TAG = "habit_ui";

// ---------------------------------------------------------------------------
// 配色与布局
// ---------------------------------------------------------------------------
#define UI_BG     0x101425  // 深靛蓝底
#define UI_PANEL  0x1C2238  // 卡片
#define UI_INK    0xE6E9F5  // 主文字
#define UI_DONE   0x5BE39A  // 已打卡(像素绿)
#define UI_TODO   0x39415F  // 未打卡/次要文字(暗蓝灰)
#define UI_FOCUS  0xFFD44D  // 选中(黄)
#define UI_LINE   0x2A3150  // 未选中描边
#define UI_WARN   0xFF6B6B  // 提示(红)

#define SCR_W 240
#define SCR_H 320

// 圆角遮罩在四角各占 30px,内容至少内缩 16px 才不会被切到。
#define MARGIN_X 16
#define TOP_BAR_H 30

#define MENU_ROW_X MARGIN_X
#define MENU_ROW_W (SCR_W - MARGIN_X * 2)
#define MENU_ROW_H 64
#define MENU_ROW_Y(i) (46 + (i) * (MENU_ROW_H + 4))

#define REC_LABEL_X MARGIN_X
#define REC_CELL_X 70
#define REC_CELL_W 18
#define REC_CELL_STEP 22
#define REC_DAYS 7

typedef enum {
    PAGE_MENU = 0,
    PAGE_CONFIRM,
    PAGE_RESULT,
    PAGE_RECORDS,
    PAGE_DATE,
} page_t;

static habit_ui_state_t *s_state;
static lv_obj_t *s_scr;
static page_t s_page;

// 主菜单
static lv_obj_t *s_row[HABIT_COUNT];
static lv_obj_t *s_marker[HABIT_COUNT];
static lv_obj_t *s_status[HABIT_COUNT];
static lv_obj_t *s_date_label;
static lv_obj_t *s_battery_label;

// 打卡确认
static lv_obj_t *s_confirm_status;
static lv_obj_t *s_confirm_action;

// 结果页
static lv_timer_t *s_result_timer;
static int s_result_ticks;
static lv_obj_t *s_sparkle[4];

// 记录页
static lv_obj_t *s_rec_frame[HABIT_COUNT];
static lv_obj_t *s_rec_cell[HABIT_COUNT][REC_DAYS];
static lv_obj_t *s_rec_streak;
// 空记录页没有格子控件。必须记住这一点:否则按上/下键时 records_refresh()
// 会去解引用上一页遗留、已被释放的指针。
static bool s_rec_has_grid;

// 日期页
static lv_obj_t *s_date_num[3];
static lv_obj_t *s_date_underline[3];
static bool s_date_first_time;

// ---------------------------------------------------------------------------
// 基础控件
// ---------------------------------------------------------------------------

// 所有可见对象都走这里:显式清零圆角/内边距/投影,避免默认主题把界面画圆。
static lv_obj_t *box_create(lv_obj_t *parent, int x, int y, int w, int h,
                            uint32_t bg, uint32_t border, int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_bg_opa(obj, bg == 0 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if (bg != 0) lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    return obj;
}

static lv_obj_t *text_create(lv_obj_t *parent, const lv_font_t *font,
                             uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *screen_create(void)
{
    lv_obj_t *scr = box_create(NULL, 0, 0, SCR_W, SCR_H, UI_BG, UI_BG, 0);
    return scr;
}

static const char *weekday_text(int32_t day)
{
    static const char *const names[7] = {
        STR_WEEKDAY_1, STR_WEEKDAY_2, STR_WEEKDAY_3, STR_WEEKDAY_4,
        STR_WEEKDAY_5, STR_WEEKDAY_6, STR_WEEKDAY_7,
    };
    const int8_t idx = habit_date_weekday(day);
    return names[((idx % 7) + 7) % 7];
}

// 顶部条:左边日期,右边电量。电量以百分比呈现(仓库规则允许百分比形式),
// 读不到(-1)时整项隐藏,而不是画一个假的数字。
static void top_bar_create(lv_obj_t *scr)
{
    box_create(scr, 0, TOP_BAR_H, SCR_W, 2, UI_LINE, UI_LINE, 0);

    const habit_date_t date = habit_date_from_days(s_state->today);
    s_date_label = text_create(scr, &app_font_12, UI_INK, "");
    lv_label_set_text_fmt(s_date_label, STR_DATE_FORMAT, date.month, date.day,
                          weekday_text(s_state->today));
    lv_obj_align(s_date_label, LV_ALIGN_TOP_LEFT, MARGIN_X, 9);

    s_battery_label = text_create(scr, &app_font_12, UI_TODO, "");
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -MARGIN_X, 9);
}

// 电量取自 state 里的缓存值,不在这里调 bsp_battery_soc()。
// 原因:CW2017 的 I2C 事务超时可达 100ms,而本函数是在**持有 LVGL 锁**时被调用的
// (menu_build / habit_ui_tick)。把一次可能阻塞 100ms 的 I2C 读放在渲染锁里会
// 直接卡住刷屏。读取动作留给应用层在不持锁的事件任务里做。
static void battery_refresh(void)
{
    const int soc = s_state->battery_soc;
    if (soc < 0) {
        lv_obj_add_flag(s_battery_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_battery_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_battery_label, STR_BATTERY, soc);
    lv_obj_set_style_text_color(
        s_battery_label, lv_color_hex(soc < 20 ? UI_WARN : UI_INK), 0);
}

static void page_begin(page_t page)
{
    // 结果页的定时器会访问即将被删除的对象,必须先停掉。
    if (s_result_timer) {
        lv_timer_delete(s_result_timer);
        s_result_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_scr = screen_create();
    s_page = page;
    lv_screen_load(s_scr);
}

// ---------------------------------------------------------------------------
// 页面:主菜单
// ---------------------------------------------------------------------------
static void menu_refresh(void)
{
    for (int i = 0; i < HABIT_COUNT; i++) {
        const bool done = (habit_records_mask(&s_state->records, s_state->today)
                           & (uint8_t)(1u << i)) != 0;
        const bool selected = (i == s_state->menu_selected);

        lv_obj_set_style_border_width(s_row[i], selected ? 3 : 2, 0);
        lv_obj_set_style_border_color(s_row[i],
            lv_color_hex(selected ? UI_FOCUS : UI_LINE), 0);
        lv_obj_set_style_bg_opa(s_marker[i], done ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_marker[i],
            lv_color_hex(done ? UI_DONE : UI_TODO), 0);
        lv_label_set_text(s_status[i], done ? STR_DONE_MARK : STR_TODO_MARK);
        lv_obj_set_style_text_color(s_status[i],
            lv_color_hex(done ? UI_DONE : UI_TODO), 0);
    }
}

static void menu_build(void)
{
    page_begin(PAGE_MENU);
    top_bar_create(s_scr);
    battery_refresh();

    static const char *const labels[HABIT_COUNT] = {
        HABIT_LABEL_SLEEP, HABIT_LABEL_EXERCISE, HABIT_LABEL_QUIT,
    };
    for (int i = 0; i < HABIT_COUNT; i++) {
        s_row[i] = box_create(s_scr, MENU_ROW_X, MENU_ROW_Y(i), MENU_ROW_W,
                              MENU_ROW_H, UI_PANEL, UI_LINE, 2);
        // 像素方块指示:已打卡实心绿,未打卡空心描边。
        s_marker[i] = box_create(s_row[i], 14, (MENU_ROW_H - 14) / 2, 14, 14, 0,
                                 UI_TODO, 2);
        lv_obj_t *name = text_create(s_row[i], &app_font_24, UI_INK, labels[i]);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 0);
        s_status[i] = text_create(s_row[i], &app_font_12, UI_TODO, "");
        lv_obj_align(s_status[i], LV_ALIGN_RIGHT_MID, -14, 0);
    }

    lv_obj_t *hint = text_create(s_scr, &app_font_12, UI_TODO, STR_MENU_HINT);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_t *hint2 = text_create(s_scr, &app_font_12, UI_TODO, STR_MENU_HINT_REC);
    lv_obj_align(hint2, LV_ALIGN_TOP_MID, 0, 282);

    menu_refresh();
}

// ---------------------------------------------------------------------------
// 页面:打卡确认
// ---------------------------------------------------------------------------
static void confirm_build(void)
{
    page_begin(PAGE_CONFIRM);
    const habit_id_t habit = (habit_id_t)s_state->menu_selected;
    const bool done = (habit_records_mask(&s_state->records, s_state->today)
                       & (uint8_t)(1u << habit)) != 0;

    static const char *const labels[HABIT_COUNT] = {
        HABIT_LABEL_SLEEP, HABIT_LABEL_EXERCISE, HABIT_LABEL_QUIT,
    };
    lv_obj_t *panel = box_create(s_scr, MARGIN_X, 48, SCR_W - MARGIN_X * 2, 176,
                                 UI_PANEL, UI_LINE, 2);
    lv_obj_t *name = text_create(panel, &app_font_24, UI_INK, labels[habit]);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 26);

    s_confirm_status = text_create(panel, &app_font_12,
                                   done ? UI_DONE : UI_TODO,
                                   done ? STR_TODAY_DONE : STR_TODAY_TODO);
    lv_obj_align(s_confirm_status, LV_ALIGN_TOP_MID, 0, 86);

    // 已打卡时不显示"确定打卡",否则会误导用户以为再按一次还有效果。
    s_confirm_action = text_create(panel, &app_font_12, UI_DONE,
                                   done ? "" : STR_CONFIRM_TODO);
    lv_obj_align(s_confirm_action, LV_ALIGN_TOP_MID, 0, 126);

    lv_obj_t *hint = text_create(s_scr, &app_font_12, UI_TODO, STR_BACK_HINT);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 282);
}

// ---------------------------------------------------------------------------
// 页面:结果
// ---------------------------------------------------------------------------
#define RESULT_TICKS 8  // 8 × 150ms ≈ 1.2s 后自动回主菜单

static void result_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    // 闪烁用"两帧交替"而不是渐变:点阵风格里突变更贴合观感。
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_opa(s_sparkle[i],
            (s_result_ticks % 2 == 0) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (++s_result_ticks >= RESULT_TICKS) {
        // 在自己回调里删除自己是安全的:LVGL 的 lv_timer_handler 会先保存
        // 下一个定时器再执行回调。这里必须先删,否则 menu_build() 删掉屏幕后
        // 定时器还会继续访问已经释放的火花控件。
        lv_timer_delete(s_result_timer);
        s_result_timer = NULL;
        menu_build();
    }
}

static void result_build(const char *text, uint32_t color)
{
    page_begin(PAGE_RESULT);
    s_result_ticks = 0;

    lv_obj_t *label = text_create(s_scr, &app_font_24, color, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 140);

    // 四角像素火花。
    const int xs[4] = { 76, 156, 76, 156 };
    const int ys[4] = { 104, 104, 180, 180 };
    for (int i = 0; i < 4; i++) {
        s_sparkle[i] = box_create(s_scr, xs[i], ys[i], 8, 8, color, color, 0);
    }

    s_result_timer = lv_timer_create(result_timer_cb, 150, NULL);
}

// ---------------------------------------------------------------------------
// 页面:记录
// ---------------------------------------------------------------------------
static void records_refresh(void)
{
    for (int row = 0; row < HABIT_COUNT; row++) {
        lv_obj_set_style_border_color(s_rec_frame[row],
            lv_color_hex(row == s_state->record_selected ? UI_FOCUS : UI_LINE), 0);
        lv_obj_set_style_border_width(s_rec_frame[row],
            row == s_state->record_selected ? 2 : 1, 0);
    }
    lv_label_set_text_fmt(s_rec_streak, STR_STREAK,
        (int)habit_records_streak(&s_state->records, s_state->today,
                                  (habit_id_t)s_state->record_selected));
}

static void records_build(void)
{
    page_begin(PAGE_RECORDS);

    lv_obj_t *title = text_create(s_scr, &app_font_24, UI_INK, STR_RECORD_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    s_rec_has_grid = habit_records_has_any(&s_state->records);
    if (!s_rec_has_grid) {
        lv_obj_t *empty = text_create(s_scr, &app_font_12, UI_TODO, STR_NO_RECORD);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 150);
        lv_obj_t *hint = text_create(s_scr, &app_font_12, UI_TODO, STR_BACK_HINT);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 282);
        return;
    }

    static const char *const labels[HABIT_COUNT] = {
        HABIT_LABEL_SLEEP, HABIT_LABEL_EXERCISE, HABIT_LABEL_QUIT,
    };
    // 列顺序:最左是 6 天前,最右是今天。
    for (int col = 0; col < REC_DAYS; col++) {
        const int32_t day = s_state->today - (REC_DAYS - 1 - col);
        lv_obj_t *head = text_create(s_scr, &app_font_12, UI_TODO, weekday_text(day));
        lv_obj_align(head, LV_ALIGN_TOP_LEFT,
                     REC_CELL_X + col * REC_CELL_STEP + 3, 56);
    }

    for (int row = 0; row < HABIT_COUNT; row++) {
        const int y = 78 + row * 32;
        s_rec_frame[row] = box_create(s_scr, 12, y - 2, SCR_W - 24, 30, 0, UI_LINE, 1);
        lv_obj_t *label = text_create(s_scr, &app_font_12, UI_INK, labels[row]);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, REC_LABEL_X, y + 5);

        for (int col = 0; col < REC_DAYS; col++) {
            const int32_t day = s_state->today - (REC_DAYS - 1 - col);
            const bool done =
                (habit_records_mask(&s_state->records, day)
                 & (uint8_t)(1u << row)) != 0;
            s_rec_cell[row][col] = box_create(s_scr, REC_CELL_X + col * REC_CELL_STEP,
                                              y + 4, REC_CELL_W, REC_CELL_W,
                                              done ? UI_DONE : 0,
                                              done ? UI_DONE : UI_TODO, 2);
        }
    }

    s_rec_streak = text_create(s_scr, &app_font_12, UI_INK, "");
    lv_obj_align(s_rec_streak, LV_ALIGN_TOP_MID, 0, 190);

    lv_obj_t *hint = text_create(s_scr, &app_font_12, UI_TODO, STR_BACK_HINT);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 282);

    records_refresh();
}

// ---------------------------------------------------------------------------
// 页面:日期设置 / 确认
// ---------------------------------------------------------------------------
static const int k_date_x[3] = { 34, 108, 162 };
static const int k_date_w[3] = { 64, 44, 44 };
#define DATE_UNIT_W 12   // 12px 字级下的汉字为全宽,宽度确定
#define DATE_NUM_H 118
#define DATE_UNIT_Y 132

static void date_refresh(void)
{
    const int values[3] = { (int)s_state->edit_date.year, s_state->edit_date.month,
                            s_state->edit_date.day };
    for (int i = 0; i < 3; i++) {
        lv_label_set_text_fmt(s_date_num[i], "%d", values[i]);
        const bool active = (i == s_state->edit_field);
        lv_obj_set_style_bg_opa(s_date_underline[i],
                                active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

static void date_build(bool first_time)
{
    page_begin(PAGE_DATE);
    s_date_first_time = first_time;

    lv_obj_t *title = text_create(s_scr, &app_font_24, UI_INK,
                                  first_time ? STR_DATE_SET : STR_DATE_CONFIRM);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    if (!first_time) {
        // 掉电后日期不可信:明确告知,而不是假装准确。
        lv_obj_t *warn = text_create(s_scr, &app_font_12, UI_WARN, STR_DATE_SUSPECT);
        lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, 62);
    }

    static const char *const units[3] = { STR_UNIT_YEAR, STR_UNIT_MONTH, STR_UNIT_DAY };
    for (int i = 0; i < 3; i++) {
        box_create(s_scr, k_date_x[i], 110, k_date_w[i], 44, 0, UI_LINE, 1);

        // 数字用固定宽度 + 右对齐:这样 1 位数与 2 位数不会让右边的"月/日"跳动,
        // 4 位数的年份也不会压到单位字上(先前左对齐的写法就会重叠)。
        s_date_num[i] = text_create(s_scr, &app_font_24, UI_INK, "0");
        lv_obj_set_width(s_date_num[i], k_date_w[i] - DATE_UNIT_W - 2);
        lv_obj_set_style_text_align(s_date_num[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_date_num[i], LV_ALIGN_TOP_LEFT, k_date_x[i], DATE_NUM_H);

        lv_obj_t *unit = text_create(s_scr, &app_font_12, UI_TODO, units[i]);
        lv_obj_align(unit, LV_ALIGN_TOP_LEFT,
                     k_date_x[i] + k_date_w[i] - DATE_UNIT_W, DATE_UNIT_Y);

        s_date_underline[i] = box_create(s_scr, k_date_x[i], 158, k_date_w[i], 4,
                                         UI_FOCUS, UI_FOCUS, 0);
    }

    lv_obj_t *h1 = text_create(s_scr, &app_font_12, UI_TODO, STR_ADJUST_HINT);
    lv_obj_align(h1, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_t *h2 = text_create(s_scr, &app_font_12, UI_TODO, STR_NEXT_HINT);
    lv_obj_align(h2, LV_ALIGN_TOP_MID, 0, 282);

    date_refresh();
}

// ---------------------------------------------------------------------------
// 按键分发
// ---------------------------------------------------------------------------
static habit_ui_effect_t menu_key(bsp_btn_t btn, bsp_btn_ev_t event)
{
    if (event == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        const int delta = (btn == BSP_BTN_DOWN) ? 1 : HABIT_COUNT - 1;
        s_state->menu_selected = (s_state->menu_selected + delta) % HABIT_COUNT;
        menu_refresh();
    } else if (event == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        confirm_build();
    } else if (event == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        records_build();
    }
    return HABIT_UI_EFFECT_NONE;
}

static habit_ui_effect_t confirm_key(bsp_btn_t btn, bsp_btn_ev_t event)
{
    if (event == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        menu_build();
        return HABIT_UI_EFFECT_NONE;
    }
    if (event != BSP_BTN_CLICK || btn != BSP_BTN_OK) return HABIT_UI_EFFECT_NONE;

    const habit_id_t habit = (habit_id_t)s_state->menu_selected;
    const habit_checkin_result_t result =
        habit_records_check_in(&s_state->records, s_state->today, habit);
    if (result == HABIT_CHECKIN_OK) {
        result_build(STR_CHECKED_IN, UI_DONE);
        return HABIT_UI_EFFECT_RECORDS_CHANGED;
    }
    result_build(STR_ALREADY, UI_TODO);
    // 重复打卡不产生写入,也不返回副作用 —— 避免无意义的 Flash 写入。
    return HABIT_UI_EFFECT_NONE;
}

static habit_ui_effect_t records_key(bsp_btn_t btn, bsp_btn_ev_t event)
{
    if (event == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        menu_build();
        return HABIT_UI_EFFECT_NONE;
    }
    if (event == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        // 空记录页没有可刷新的控件,直接忽略,避免触碰上一页释放掉的指针。
        if (!s_rec_has_grid) return HABIT_UI_EFFECT_NONE;
        const int delta = (btn == BSP_BTN_DOWN) ? 1 : HABIT_COUNT - 1;
        s_state->record_selected = (s_state->record_selected + delta) % HABIT_COUNT;
        records_refresh();
    }
    return HABIT_UI_EFFECT_NONE;
}

static habit_ui_effect_t date_key(bsp_btn_t btn, bsp_btn_ev_t event)
{
    if (event == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        const int delta = (btn == BSP_BTN_UP) ? 1 : -1;
        s_state->edit_date = habit_date_adjust(
            s_state->edit_date, (habit_date_field_t)s_state->edit_field, delta);
        date_refresh();
        return HABIT_UI_EFFECT_NONE;
    }

    if (event == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        if (s_state->edit_field < HABIT_FIELD_DAY) {
            s_state->edit_field++;
            date_refresh();
            return HABIT_UI_EFFECT_NONE;
        }
        // 最后一段确认:交给应用层更新墙钟基准并落盘。
        menu_build();
        return HABIT_UI_EFFECT_DATE_CHANGED;
    }

    // 首次设置不允许取消:否则应用没有可用日期,主菜单的日期栏就是错的。
    if (event == BSP_BTN_LONG && btn == BSP_BTN_OK && !s_date_first_time) {
        menu_build();
    }
    return HABIT_UI_EFFECT_NONE;
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
void habit_ui_show_menu(habit_ui_state_t *state)
{
    s_state = state;
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL 加锁超时,无法建立界面");
        return;
    }
    menu_build();
    bsp_lvgl_unlock();
}

void habit_ui_show_date(habit_ui_state_t *state, bool first_time)
{
    s_state = state;
    // 首次设置从年份开始(要从头定日期);掉电后确认则从"日"开始 ——
    // 那种情况下用户多半只需要修正到当天,少按两下确认键。
    s_state->edit_field = first_time ? HABIT_FIELD_YEAR : HABIT_FIELD_DAY;
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL 加锁超时,无法建立日期页");
        return;
    }
    date_build(first_time);
    bsp_lvgl_unlock();
}

habit_ui_effect_t habit_ui_handle_key(habit_ui_state_t *state, bsp_btn_t btn,
                                      bsp_btn_ev_t event)
{
    s_state = state;
    if (!bsp_lvgl_lock(500)) return HABIT_UI_EFFECT_NONE;

    habit_ui_effect_t effect;
    switch (s_page) {
    case PAGE_CONFIRM: effect = confirm_key(btn, event); break;
    case PAGE_RECORDS: effect = records_key(btn, event); break;
    case PAGE_DATE:    effect = date_key(btn, event); break;
    case PAGE_RESULT:
        // 结果页等待自动返回;此时按键只用来跳过等待。
        if (btn == BSP_BTN_OK) menu_build();
        effect = HABIT_UI_EFFECT_NONE;
        break;
    case PAGE_MENU:
    default:           effect = menu_key(btn, event); break;
    }

    bsp_lvgl_unlock();
    return effect;
}

void habit_ui_show_save_failed(void)
{
    if (!bsp_lvgl_lock(500)) return;
    // 覆盖先前的"打卡成功"提示:落盘失败比成功更值得让用户看到,
    // 否则用户以为记下了,实际上重启就没了。
    result_build(STR_RECORD_FAILED, UI_WARN);
    bsp_lvgl_unlock();
}

void habit_ui_tick(void)
{
    if (!bsp_lvgl_lock(200)) return;
    // 只有主菜单有顶栏电量,其它页面不需要周期刷新。
    if (s_page == PAGE_MENU && s_battery_label) battery_refresh();
    bsp_lvgl_unlock();
}

void habit_ui_release(void)
{
    if (!bsp_lvgl_lock(500)) return;
    if (s_result_timer) {
        lv_timer_delete(s_result_timer);
        s_result_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    bsp_lvgl_unlock();
}