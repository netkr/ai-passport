#include "habit_device.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rtc_time.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "habit_dev";

#define HABIT_NVS_NAMESPACE "habit"
#define HABIT_NVS_KEY_RECORDS "records"

// RTC 保留内存:深睡与普通重启后保留,掉电清零 —— 所以首次上电 magic 必为 0,
// 不需要额外的方式去"识别"首次开机。
RTC_DATA_ATTR static habit_clock_mark_t s_mark;

esp_err_t habit_device_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 只在这两种"NVS 分区确实无法使用"的官方情形下重建,其它错误一律如实
        // 上报:擦除会连带清掉别的命名空间,不能用"擦一下试试"掩盖问题。
        ESP_LOGW(TAG, "NVS 分区不可用(%s),擦除后重建", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
    }
    return err;
}

uint64_t habit_device_rtc_us(void)
{
    return esp_rtc_get_time_us();
}

const habit_clock_mark_t *habit_device_mark(void)
{
    return &s_mark;
}

void habit_device_save_mark(const habit_clock_mark_t *mark)
{
    s_mark = *mark;
}

esp_err_t habit_device_load(habit_store_data_t *out, bool *found)
{
    *found = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(HABIT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;  // 首次开局,尚无记录
    if (err != ESP_OK) return err;

    uint8_t buf[HABIT_STORE_ENCODED_SIZE];
    size_t len = sizeof(buf);
    err = nvs_get_blob(handle, HABIT_NVS_KEY_RECORDS, buf, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    if (!habit_store_decode(buf, len, out)) {
        // 布局不认识或数据损坏:按"无记录"处理,让用户走一次确认流程,
        // 而不是把可疑数据当成真实打卡记录展示出来。
        ESP_LOGW(TAG, "记录解码失败(len=%u),按无记录处理", (unsigned)len);
        return ESP_OK;
    }

    *found = true;
    return ESP_OK;
}

esp_err_t habit_device_save(const habit_records_t *records, int32_t today, int64_t wall_sec)
{
    uint8_t buf[HABIT_STORE_ENCODED_SIZE];
    const size_t len = habit_store_encode(records, today, wall_sec, buf, sizeof(buf));
    if (len == 0) return ESP_ERR_INVALID_SIZE;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(HABIT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, HABIT_NVS_KEY_RECORDS, buf, len);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}