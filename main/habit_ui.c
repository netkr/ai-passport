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
// 一屏只放 3 行,多出来的类别靠窗口滚动。行对象数量固定为 3,不随类别数增长;
// 起点由选中项推出(见 menu_window_start),选中项移出窗口时整窗平移。
#define MENU_VISIBLE 3
#define MENU_ROW_GAP 4
#define MENU_ROW_Y(i) (54 + (i) * (MENU_ROW_H + MENU_ROW_GAP))
// 顶栏下方那条状态行(左:今日完成度;右:选中项连续天数)。放在这里是因为
// 主菜单的第二条提示在底部,而进度需要比提示更靠近视线中心。
#define MENU_STATUS_Y 34

// 记录页:整月网格。
//
// 7 列 × 6 行是任何月份都装得下的最小固定尺寸(1 号是周日且当月 31 天时占用
// 37 格,向上取整为 6 行)。结构固定,换月只是刷新 42 个格子的内容与样式,
// 不重建页面 —— 因此切月不会产生一批新的 LVGL 对象。
#define CAL_COLS 7
#define CAL_ROWS 6
#define CAL_CELLS (CAL_COLS * CAL_ROWS)
#define CAL_CELL_W 26
#define CAL_CELL_H 24
#define CAL_GAP 2
#define CAL_X0 ((SCR_W - (CAL_COLS * CAL_CELL_W + (CAL_COLS - 1) * CAL_GAP)) / 2)
#define CAL_Y0 78
#define CAL_CELL_X(col) (CAL_X0 + (col) * (CAL_CELL_W + CAL_GAP))
#define CAL_CELL_Y(row) (CAL_Y0 + (row) * (CAL_CELL_H + CAL_GAP))
// 最旧可看的月份:环形槽只有 HABIT_RECORD_SLOTS 天,更早的日期读出来必然是空的,
// 让用户翻过去只会看到一片空网格。
#define CAL_OLDEST_DAYS (HABIT_RECORD_SLOTS - 1)

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

// 类别名称与 habit_id_t 一一对应,只在本文件定义一次。
static const char *const k_habit_labels[HABIT_COUNT] = {
    HABIT_LABEL_SLEEP, HABIT_LABEL_EXERCISE, HABIT_LABEL_QUIT,
    HABIT_LABEL_READ,
};
// 少写一项会让后面的槽位变成空指针,所以让编译器在构建期拦住。
_Static_assert(sizeof(k_habit_labels) / sizeof(k_habit_labels[0]) == HABIT_COUNT,
               "k_habit_labels 必须覆盖全部 habit_id_t");
// 窗口有 MENU_VISIBLE 行,类别更少时窗口会去读不存在的类别名;在构建期挡住,
// 运行期就不必再为这种情形加分支。
_Static_assert(HABIT_COUNT >= MENU_VISIBLE, "类别数少于菜单可见行数");

// 主菜单(行对象只建 MENU_VISIBLE 个,内容随窗口滚动刷新)
static lv_obj_t *s_row[MENU_VISIBLE];
static lv_obj_t *s_row_name[MENU_VISIBLE];
static lv_obj_t *s_marker[MENU_VISIBLE];
static lv_obj_t *s_status[MENU_VISIBLE];
static lv_obj_t *s_progress_label;
static lv_obj_t *s_menu_streak;
static lv_obj_t *s_date_label;
static lv_obj_t *s_battery_label;

// 打卡确认
static lv_obj_t *s_confirm_status;
static lv_obj_t *s_confirm_action;

// 结果页
static lv_timer_t *s_result_timer;
static int s_result_ticks;
static lv_obj_t *s_sparkle[4];
// 3 秒撤销窗口:s_result_habit 是被撤销的类别(<0 表示这一页不可撤销)。
static int s_result_habit;
static bool s_result_undoable;

// 记录页(月历):网格结构与 42 个格子只建一次,换月/换项只刷新内容。
//
// ⚠ 每格只用【一个】对象:label 自己带底色与描边,日期数字就是它的文本。
// 早先的写法是"格子容器 + 内部标签"两个对象,42 格共 84 个,实测在 LVGL 内置
// 内存池里建到第 3 行就耗尽(每行约 3.4KB),随后整个界面永久卡死。
static int32_t s_cal_month;   // 当前显示的月份(月序号)
static lv_obj_t *s_cal_title;
static lv_obj_t *s_cal_habit;
static lv_obj_t *s_cal_streak;
static lv_obj_t *s_cal_cell[CAL_CELLS];

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

// 星期名按索引取(0=周一 .. 6=周日)。表头用索引,日期用天数换算成索引。
static const char *weekday_name(int index)
{
    static const char *const names[7] = {
        STR_WEEKDAY_1, STR_WEEKDAY_2, STR_WEEKDAY_3, STR_WEEKDAY_4,
        STR_WEEKDAY_5, STR_WEEKDAY_6, STR_WEEKDAY_7,
    };
    return names[((index % 7) + 7) % 7];
}

static const char *weekday_text(int32_t day)
{
    return weekday_name(habit_date_weekday(day));
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

// 窗口起点,让选中项尽量落在中间一行。夹在 [0, HABIT_COUNT - MENU_VISIBLE]:
// 到首尾时窗口不再平移 —— 否则最后几项会推出空白行。
static int menu_window_start(void)
{
    int start = s_state->menu_selected - MENU_VISIBLE / 2;
    const int last = HABIT_COUNT - MENU_VISIBLE;
    if (start < 0) start = 0;
    if (start > last) start = last;
    return start;
}

static void menu_refresh(void)
{
    const int start = menu_window_start();
    const uint8_t today_mask = habit_records_mask(&s_state->records, s_state->today);

    for (int i = 0; i < MENU_VISIBLE; i++) {
        const int habit = start + i;
        const bool done = (today_mask & (uint8_t)(1u << habit)) != 0;
        const bool selected = (habit == s_state->menu_selected);

        // 行对象是复用的,所以窗口滚动时名称也要跟着换。
        lv_label_set_text(s_row_name[i], k_habit_labels[habit]);
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

    // 进度与连续天数都取自纯逻辑层:这里只做格式化,不重算规则。
    lv_label_set_text_fmt(s_progress_label, STR_TODAY_PROGRESS,
                          (int)habit_records_day_count(&s_state->records, s_state->today),
                          (int)HABIT_COUNT);
    lv_label_set_text_fmt(s_menu_streak, STR_STREAK,
        (int)habit_records_streak(&s_state->records, s_state->today,
                                  (habit_id_t)s_state->menu_selected));
}

static void menu_build(void)
{
    page_begin(PAGE_MENU);
    top_bar_create(s_scr);
    battery_refresh();

    s_progress_label = text_create(s_scr, &app_font_12, UI_TODO, "");
    lv_obj_align(s_progress_label, LV_ALIGN_TOP_LEFT, MARGIN_X, MENU_STATUS_Y);
    s_menu_streak = text_create(s_scr, &app_font_12, UI_TODO, "");
    lv_obj_align(s_menu_streak, LV_ALIGN_TOP_RIGHT, -MARGIN_X, MENU_STATUS_Y);

    for (int i = 0; i < MENU_VISIBLE; i++) {
        s_row[i] = box_create(s_scr, MENU_ROW_X, MENU_ROW_Y(i), MENU_ROW_W,
                              MENU_ROW_H, UI_PANEL, UI_LINE, 2);
        // 像素方块指示:已打卡实心绿,未打卡空心描边。
        s_marker[i] = box_create(s_row[i], 14, (MENU_ROW_H - 14) / 2, 14, 14, 0,
                                 UI_TODO, 2);
        // 文本先留空:由 menu_refresh() 按窗口起点填入对应类别的名称。
        s_row_name[i] = text_create(s_row[i], &app_font_24, UI_INK, "");
        lv_obj_align(s_row_name[i], LV_ALIGN_LEFT_MID, 40, 0);
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

    lv_obj_t *panel = box_create(s_scr, MARGIN_X, 48, SCR_W - MARGIN_X * 2, 176,
                                 UI_PANEL, UI_LINE, 2);
    lv_obj_t *name = text_create(panel, &app_font_24, UI_INK, k_habit_labels[habit]);
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
//
// 打卡成功这一页同时是撤销窗口:3 秒内按确定撤回这次打卡。其它结果(重复打卡、
// 保存失败)没有可撤销的动作,也不该让人误以为按确定能做什么。
// ---------------------------------------------------------------------------
#define RESULT_TICK_MS 150
#define RESULT_TICKS_INFO 8    // 8 × 150ms = 1.2s:普通提示
#define RESULT_TICKS_UNDO 20   // 20 × 150ms = 3.0s:留出撤销窗口

static void result_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    // 闪烁用"两帧交替"而不是渐变:点阵风格里突变更贴合观感。
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_opa(s_sparkle[i],
            (s_result_ticks % 2 == 0) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    const int limit = s_result_undoable ? RESULT_TICKS_UNDO : RESULT_TICKS_INFO;
    if (++s_result_ticks >= limit) {
        // 在自己回调里删除自己是安全的:LVGL 的 lv_timer_handler 会先保存
        // 下一个定时器再执行回调。这里必须先删,否则 menu_build() 删掉屏幕后
        // 定时器还会继续访问已经释放的火花控件。
        lv_timer_delete(s_result_timer);
        s_result_timer = NULL;
        // 窗口已过,清掉标记,避免这一页退出后还留着"可撤销"的假状态。
        s_result_undoable = false;
        menu_build();
    }
}

// undo_habit < 0 表示这一页不可撤销(结果不是一次成功的打卡)。
static void result_build(const char *text, uint32_t color, int undo_habit)
{
    page_begin(PAGE_RESULT);
    s_result_ticks = 0;
    s_result_habit = undo_habit;
    s_result_undoable = (undo_habit >= 0);

    lv_obj_t *label = text_create(s_scr, &app_font_24, color, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 140);

    // 四角像素火花。
    const int xs[4] = { 76, 156, 76, 156 };
    const int ys[4] = { 104, 104, 180, 180 };
    for (int i = 0; i < 4; i++) {
        s_sparkle[i] = box_create(s_scr, xs[i], ys[i], 8, 8, color, color, 0);
    }

    // 撤销提示只出现在真的可撤销时:不可撤销的结果页上写着"确定撤销"是骗人。
    if (s_result_undoable) {
        lv_obj_t *undo = text_create(s_scr, &app_font_12, UI_TODO, STR_UNDO_HINT);
        lv_obj_align(undo, LV_ALIGN_TOP_MID, 0, 196);
    }

    s_result_timer = lv_timer_create(result_timer_cb, RESULT_TICK_MS, NULL);
}

// ---------------------------------------------------------------------------
// 页面:记录(整月热力图)
//
// 每格一天:已打卡整格填绿,今天用黄色描边标出,非本月的格子直接隐藏。
// 三者都是"换月只改样式/文本"的前提 —— 42 个格子从一开始就存在。
// ---------------------------------------------------------------------------
static void calendar_refresh(void)
{
    const int32_t first = habit_month_first_day(s_cal_month);
    const habit_date_t month = habit_date_from_days(first);
    const int days = (int)habit_date_days_in_month(month.year, month.month);
    // 1 号是星期几(0=周一),也就是网格前面要空出的格数。
    const int lead = habit_date_weekday(first);
    const uint8_t bit = (uint8_t)(1u << s_state->record_selected);

    lv_label_set_text_fmt(s_cal_title, STR_MONTH_FORMAT, (int)month.year, (int)month.month);
    lv_label_set_text(s_cal_habit, k_habit_labels[s_state->record_selected]);
    if (habit_records_has_any(&s_state->records)) {
        lv_label_set_text_fmt(s_cal_streak, STR_STREAK,
            (int)habit_records_streak(&s_state->records, s_state->today,
                                      (habit_id_t)s_state->record_selected));
    } else {
        lv_label_set_text(s_cal_streak, STR_NO_RECORD);
    }

    for (int cell = 0; cell < CAL_CELLS; cell++) {
        const int offset = cell - lead;   // 该格是当月第几天(0 基)
        if (offset < 0 || offset >= days) {
            lv_obj_add_flag(s_cal_cell[cell], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_cal_cell[cell], LV_OBJ_FLAG_HIDDEN);

        const int32_t day = first + offset;
        const bool done = (habit_records_mask(&s_state->records, day) & bit) != 0;
        const bool is_today = (day == s_state->today);

        lv_label_set_text_fmt(s_cal_cell[cell], "%d", offset + 1);
        lv_obj_set_style_bg_opa(s_cal_cell[cell], done ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        // 今天的描边优先于"已打卡":否则今天就淹没在一片绿格子里。
        lv_obj_set_style_border_color(s_cal_cell[cell],
            lv_color_hex(is_today ? UI_FOCUS : (done ? UI_DONE : UI_LINE)), 0);
        lv_obj_set_style_border_width(s_cal_cell[cell], is_today ? 2 : 1, 0);
        // 绿底上用底色当字色,未打卡用主字色 —— 日期本身要看得清。
        lv_obj_set_style_text_color(s_cal_cell[cell],
            lv_color_hex(done ? UI_BG : UI_INK), 0);
    }
}

// 月历格子:label 自己当格子(底色 + 描边 + 居中日期数字),一个对象顶两个。
static lv_obj_t *cal_cell_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *cell = lv_label_create(parent);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, CAL_CELL_W, CAL_CELL_H);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_set_style_shadow_width(cell, 0, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(cell, lv_color_hex(UI_DONE), 0);
    lv_obj_set_style_text_font(cell, &app_font_12, 0);
    lv_obj_set_style_text_color(cell, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
    // 高度固定为 CAL_CELL_H,12px 的字靠上内边距压到垂直居中。
    lv_obj_set_style_pad_top(cell, (CAL_CELL_H - 12) / 2, 0);
    return cell;
}

static void records_build(void)
{
    page_begin(PAGE_RECORDS);
    // 每次进入都从当前月份开始:绝大多数时候用户想看的就是本月。
    s_cal_month = habit_month_index_of_day(s_state->today);

    s_cal_title = text_create(s_scr, &app_font_24, UI_INK, "");
    lv_obj_align(s_cal_title, LV_ALIGN_TOP_MID, 0, 14);

    // 第二行:左边当前类别,右边它的连续天数。
    s_cal_habit = text_create(s_scr, &app_font_12, UI_INK, "");
    lv_obj_align(s_cal_habit, LV_ALIGN_TOP_LEFT, MARGIN_X, 44);
    s_cal_streak = text_create(s_scr, &app_font_12, UI_TODO, "");
    lv_obj_align(s_cal_streak, LV_ALIGN_TOP_RIGHT, -MARGIN_X, 44);

    for (int col = 0; col < CAL_COLS; col++) {
        lv_obj_t *head = text_create(s_scr, &app_font_12, UI_TODO, weekday_name(col));
        lv_obj_set_width(head, CAL_CELL_W);
        lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(head, LV_ALIGN_TOP_LEFT, CAL_CELL_X(col), 62);
    }

    for (int row = 0; row < CAL_ROWS; row++) {
        for (int col = 0; col < CAL_COLS; col++) {
            const int cell = row * CAL_COLS + col;
            s_cal_cell[cell] = cal_cell_create(s_scr, CAL_CELL_X(col), CAL_CELL_Y(row));
        }
    }

    lv_obj_t *hint = text_create(s_scr, &app_font_12, UI_TODO, STR_CAL_HINT);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_t *hint2 = text_create(s_scr, &app_font_12, UI_TODO, STR_BACK_HINT);
    lv_obj_align(hint2, LV_ALIGN_TOP_MID, 0, 268);

    calendar_refresh();
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
        // 把这一次打卡的可撤销目标交给结果页:3 秒内按确定可以撤回。
        result_build(STR_CHECKED_IN, UI_DONE, (int)habit);
        return HABIT_UI_EFFECT_RECORDS_CHANGED;
    }
    result_build(STR_ALREADY, UI_TODO, -1);
    // 不产生写入,也不返回 RECORDS_CHANGED —— 避免无意义的 Flash 写入;
    // 但要单独告知"被拒",让应用层给出不同的提示音。
    return HABIT_UI_EFFECT_REJECTED;
}

static habit_ui_effect_t records_key(bsp_btn_t btn, bsp_btn_ev_t event)
{
    if (event == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        menu_build();
        return HABIT_UI_EFFECT_NONE;
    }

    if (event == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        // 换月。两端夹紧而不是环绕:未来月份没有数据可看,比环形槽更早的月份
        // 读出来必然是空的,环绕过去只会让人以为记录丢了。
        const int32_t oldest = habit_month_index_of_day(s_state->today - CAL_OLDEST_DAYS);
        const int32_t newest = habit_month_index_of_day(s_state->today);
        const int32_t wanted = s_cal_month + ((btn == BSP_BTN_UP) ? -1 : 1);
        const int32_t clamped = wanted < oldest ? oldest
                              : wanted > newest ? newest
                              : wanted;
        if (clamped != s_cal_month) {
            s_cal_month = clamped;
            calendar_refresh();
        }
        return HABIT_UI_EFFECT_NONE;
    }

    if (event == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        // 换项:环绕循环,和主菜单的上下键一样是环绕语义。
        s_state->record_selected = (s_state->record_selected + 1) % HABIT_COUNT;
        calendar_refresh();
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
        // 字段循环:DAY 之后回到 YEAR,这样从任何字段开始都能改年/月/日。
        s_state->edit_field = (s_state->edit_field + 1) % (HABIT_FIELD_DAY + 1);
        date_refresh();
        return HABIT_UI_EFFECT_NONE;
    }

    // 长按 OK 确认日期:首次设置和掉电确认都走这里。
    if (event == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        menu_build();
        return HABIT_UI_EFFECT_DATE_CHANGED;
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
        // 结果页等待自动返回。打卡成功后的那 3 秒里,确定 = 撤回这次打卡;
        // 长按 = 保留这次打卡直接返回(想按确定跳过等待的人不会误撤)。
        effect = HABIT_UI_EFFECT_NONE;
        if (btn == BSP_BTN_OK && event == BSP_BTN_CLICK) {
            if (s_result_undoable) {
                s_result_undoable = false;   // 窗口只给一次
                if (habit_records_undo(&s_state->records, s_state->today,
                                       (habit_id_t)s_result_habit)) {
                    effect = HABIT_UI_EFFECT_RECORD_UNDONE;
                }
            }
            menu_build();
        } else if (btn == BSP_BTN_OK && event == BSP_BTN_LONG) {
            s_result_undoable = false;
            menu_build();
        }
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
    result_build(STR_RECORD_FAILED, UI_WARN, -1);
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