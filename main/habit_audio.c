// main/habit_audio.c —— 提示音实现。
//
// 两条来自硬件指南的约束决定了这个结构:
//   · `bsp_audio_write` 会阻塞(写 PCM 是阻塞调用),所以绝不能放在按键回调或
//     LVGL 任务里 —— 本模块用独立任务,调用方只投递请求。
//   · 每次播放前重新设置格式:同格式重复调用很廉价,而且音频被休眠/唤醒后
//     (休眠会释放 codec 对象)能自动恢复,不需要额外的状态跟踪。
#include "habit_audio.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "habit_audio";

#define SAMPLE_RATE      16000
#define CHUNK_SAMPLES    512
#define AUDIO_TASK_STACK 3072
#define AUDIO_TASK_PRIO  4
#define AMPLITUDE        7000
#define FADE_SAMPLES     32   // 约 2ms @16kHz
#define VOLUME           75

// 挂起握手的等待上限。一次提示音最长约 260ms,加上写 PCM 的阻塞时间,
// 1 秒足够;超时说明任务卡在驱动里,此时绝不能继续把 codec 挂起。
#define SUSPEND_TIMEOUT_MS 1000

typedef struct {
    uint16_t freq_hz;
    uint16_t ms;
} note_t;

// 方波而不是正弦:点阵像素风配方波才协调,而且完全不需要浮点运算。
// 两短音上行 = 成功;一声低沉 = 被拒。
static const note_t k_ok_notes[] = { { 880, 70 }, { 1319, 90 } };
static const note_t k_rejected_notes[] = { { 330, 170 } };

static TaskHandle_t s_task;
static int16_t s_buf[CHUNK_SAMPLES];

// 挂起握手。请求用标志而不是通知值表达:通知值已被提示音种类占用,而"请求/撤销"
// 必须能和"还有一声提示音待播"区分开 —— 否则撤销时的通知会被当成一次播放。
static volatile bool s_suspend_requested;
static volatile bool s_suspended;
static SemaphoreHandle_t s_suspended_ack;

static void play_note(const note_t *note)
{
    const int period = SAMPLE_RATE / note->freq_hz;
    const int total = SAMPLE_RATE * note->ms / 1000;
    int remaining = total;
    int emitted = 0;
    int phase = 0;

    while (remaining > 0) {
        // 挂起请求可以在任何一个分块之间到达。在这里退出,收尾时间就是一个
        // 分块的时长(约 32ms @16kHz),调用方拿到的"已停写"才是真的。
        if (s_suspend_requested) return;
        const int n = remaining < CHUNK_SAMPLES ? remaining : CHUNK_SAMPLES;
        for (int i = 0; i < n; i++) {
            int32_t sample = (phase < period / 2) ? AMPLITUDE : -AMPLITUDE;
            // 起止各做一段线性淡入淡出:电平突变的方波会在喇叭上"啪"一声,
            // 每次都响的提示音里这个杂音很明显。
            const int pos = emitted + i;
            if (pos < FADE_SAMPLES) {
                sample = sample * pos / FADE_SAMPLES;
            } else if (pos >= total - FADE_SAMPLES) {
                sample = sample * (total - 1 - pos) / FADE_SAMPLES;
            }
            s_buf[i] = (int16_t)sample;
            if (++phase >= period) phase = 0;
        }
        if (bsp_audio_write(s_buf, (size_t)n * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGW(TAG, "写出 PCM 失败,本次提示音跳过");
            return;
        }
        emitted += n;
        remaining -= n;
    }
}

static void play_sound(habit_sound_t sound)
{
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "音频格式设置失败,本次提示音跳过");
        return;
    }
    bsp_audio_set_volume(VOLUME);

    if (sound == HABIT_SOUND_OK) {
        for (size_t i = 0; i < sizeof(k_ok_notes) / sizeof(k_ok_notes[0]); i++) {
            play_note(&k_ok_notes[i]);
        }
    } else {
        play_note(&k_rejected_notes[0]);
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t sound = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &sound, portMAX_DELAY) != pdTRUE) continue;

        if (s_suspend_requested) {
            // 这里已经不在写 PCM 了(play_note 已返回),可以安全地回执。
            s_suspended = true;
            xSemaphoreGive(s_suspended_ack);
            uint32_t ignored;
            while (s_suspend_requested) {
                // 撤销挂起时会再通知一次;通知是可锁存的,所以即使撤销发生在
                // 本循环之前的瞬间,这里的等待也会立刻返回。
                xTaskNotifyWait(0, UINT32_MAX, &ignored, portMAX_DELAY);
            }
            s_suspended = false;
            continue;
        }
        // 挂起期间的通知值固定为 0,不是任何提示音。
        if (sound != 0) play_sound((habit_sound_t)sound);
    }
}

esp_err_t habit_audio_init(void)
{
    const esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "音频初始化失败(%s):界面与记录不受影响,只是没有提示音",
                 esp_err_to_name(err));
        return err;
    }
    s_suspended_ack = xSemaphoreCreateBinary();
    if (s_suspended_ack == NULL ||
        xTaskCreate(audio_task, "habit_audio", AUDIO_TASK_STACK, NULL,
                    AUDIO_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGW(TAG, "提示音任务创建失败");
        if (s_suspended_ack) {
            vSemaphoreDelete(s_suspended_ack);
            s_suspended_ack = NULL;
        }
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void audio_resume(void)
{
    if (!s_suspend_requested) return;
    s_suspend_requested = false;
    s_suspended = false;
    if (s_task) xTaskNotify(s_task, 0, eSetValueWithOverwrite);
}

esp_err_t habit_audio_suspend(void)
{
    // 没有音频(初始化失败)时无可挂起,视为已停。
    if (s_task == NULL || s_suspended) return ESP_OK;

    s_suspend_requested = true;
    // 叫醒可能正阻塞在通知等待里的播放任务;正在播时靠 play_note 的分块检查退出。
    xTaskNotify(s_task, 0, eSetValueWithOverwrite);
    if (xSemaphoreTake(s_suspended_ack, pdMS_TO_TICKS(SUSPEND_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "提示音任务 %d ms 内未停止,可能卡在写 PCM", SUSPEND_TIMEOUT_MS);
        // 撤销请求:调用方会放弃本次休眠,音频必须恢复可用,而不是永久停摆。
        audio_resume();
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void habit_audio_play(habit_sound_t sound)
{
    // 挂起期间丢弃:此时写 PCM 会和深睡前的 ES8311 挂起序列抢同一份状态。
    if (s_task == NULL || s_suspend_requested || s_suspended) return;
    // 覆盖式投递:连按确定时只需要听到最后一次,不必排队积压。
    xTaskNotify(s_task, (uint32_t)sound, eSetValueWithOverwrite);
}