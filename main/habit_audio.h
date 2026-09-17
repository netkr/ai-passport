// main/habit_audio.h —— 提示音。软依赖:没有音频时应用照常跑,只是不出声。
//
// 播放会阻塞(写 PCM 是阻塞调用),所以全部在独立任务里做,调用方只投递请求。
// 这一点与基线 demo_audio.c 的结论一致,见硬件指南第 8 节。
#pragma once

#include "esp_err.h"

typedef enum {
    HABIT_SOUND_OK = 1,      // 打卡成功:两声上行短音
    HABIT_SOUND_REJECTED,    // 操作被拒(当天已打卡)/保存失败:一声低音
} habit_sound_t;

// 初始化音频并建立播放任务。失败返回错误,但不影响界面与记录功能。
esp_err_t habit_audio_init(void);

// 投递一次播放请求。不阻塞,可在事件任务里直接调用。
void habit_audio_play(habit_sound_t sound);