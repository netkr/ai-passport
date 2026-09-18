// main/main.c —— 应用入口。
//
// 派生应用的实际实现分布在:
//   habit_app.c    设备初始化、墙钟/记录恢复、事件任务、落盘
//   habit_ui.c     五个像素页面与按键导航
//   habit_date/model/clock/store   可在主机上测试的纯逻辑
//
// 基线的硬件测试示例(demo_*.c、ui_pixel*.c、demo_navigation.c)保留在本目录中
// 作为 BSP 用法的参考,但已不再参与本应用的构建 —— 见 main/CMakeLists.txt。
#include "esp_log.h"

#include "habit_app.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy AI Passport: 像素打卡");
    habit_app_run();
}