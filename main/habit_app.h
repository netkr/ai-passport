// main/habit_app.h —— 应用入口。由 app_main() 调用。
#pragma once

// 启动像素打卡应用:初始化设备、恢复墙钟与记录、建立界面并开始处理输入。
//
// 正常启动后不返回(app_main 返回时应用任务仍在运行,与基线 demo 一致);
// 只有在显示/LVGL 不可用或输入任务建不起来时才返回,此时没有可用界面。
void habit_app_run(void);