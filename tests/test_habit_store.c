#include <assert.h>
#include <string.h>

#include "habit_store.h"

#define DAY 20713
// 位常量具名化:写成 `== 0x01 | 0x04` 会因优先级解析成 `(x == 0x01) | 0x04`,
// 恒为真 —— 断言就废了。用具名常量并整体加括号。
#define BIT_SLEEP 0x01u
#define BIT_QUIT 0x04u

static void test_encoded_size_is_fixed(void)
{
    // 布局是显式的:4 magic + 2 version + 2 slots + 4 today + 8 wall + 90 槽 × 5。
    assert(HABIT_STORE_ENCODED_SIZE == 20u + 90u * 5u);
}

static void test_round_trip_preserves_records(void)
{
    habit_records_t records;
    habit_records_clear(&records);
    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);
    assert(habit_records_check_in(&records, DAY, HABIT_QUIT) == HABIT_CHECKIN_OK);
    assert(habit_records_check_in(&records, DAY - 1, HABIT_SLEEP) == HABIT_CHECKIN_OK);

    uint8_t buf[HABIT_STORE_ENCODED_SIZE];
    const size_t written = habit_store_encode(&records, DAY, 1789603200, buf, sizeof(buf));
    assert(written == HABIT_STORE_ENCODED_SIZE);

    habit_store_data_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    assert(habit_store_decode(buf, written, &loaded));

    assert(loaded.version == HABIT_STORE_VERSION);
    assert(loaded.slots == HABIT_RECORD_SLOTS);
    assert(loaded.today == DAY);
    assert(loaded.wall_sec == 1789603200);

    // 解码结果必须能直接被模型使用 —— 这才是"数据没坏"的真正判据,
    // 逐字段比对不足以保证语义一致。
    assert(habit_records_mask(&loaded.records, DAY) == (BIT_SLEEP | BIT_QUIT));
    assert(habit_records_mask(&loaded.records, DAY - 1) == BIT_SLEEP);
    assert(habit_records_mask(&loaded.records, DAY - 2) == 0);
    assert(habit_records_streak(&loaded.records, DAY, HABIT_SLEEP) == 2);
    assert(habit_records_has_any(&loaded.records));
}

static void test_empty_records_round_trip(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    uint8_t buf[HABIT_STORE_ENCODED_SIZE];
    const size_t written = habit_store_encode(&records, DAY, 0, buf, sizeof(buf));
    assert(written == HABIT_STORE_ENCODED_SIZE);

    habit_store_data_t loaded;
    memset(&loaded, 0xFF, sizeof(loaded));
    assert(habit_store_decode(buf, written, &loaded));

    // 空槽哨兵必须原样往返,否则"无记录"会被读成某个真实日期。
    for (size_t i = 0; i < HABIT_RECORD_SLOTS; i++) {
        assert(loaded.records.slots[i].day == HABIT_SLOT_EMPTY);
        assert(loaded.records.slots[i].mask == 0);
    }
    assert(!habit_records_has_any(&loaded.records));
}

static void test_encode_rejects_small_buffer(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    uint8_t buf[HABIT_STORE_ENCODED_SIZE];
    memset(buf, 0xAA, sizeof(buf));

    // 缓冲区差一个字节就必须整体拒绝,不能写入半截数据。
    assert(habit_store_encode(&records, DAY, 1, buf, sizeof(buf) - 1) == 0);
    for (size_t i = 0; i < sizeof(buf); i++) assert(buf[i] == 0xAA);

    assert(habit_store_encode(NULL, DAY, 1, buf, sizeof(buf)) == 0);
    assert(habit_store_encode(&records, DAY, 1, NULL, sizeof(buf)) == 0);
}

static void test_decode_rejects_corrupt_input(void)
{
    habit_records_t records;
    habit_records_clear(&records);
    assert(habit_records_check_in(&records, DAY, HABIT_SLEEP) == HABIT_CHECKIN_OK);

    uint8_t good[HABIT_STORE_ENCODED_SIZE];
    assert(habit_store_encode(&records, DAY, 1789603200, good, sizeof(good)) == sizeof(good));

    habit_store_data_t out;

    // 每种损坏都要拒绝,且不得改动调用方手里的数据。
    const struct { const char *what; size_t offset; uint8_t value; size_t len; } cases[] = {
        { "magic 破损", 0, 0x00, HABIT_STORE_ENCODED_SIZE },
        { "版本不认识", 4, 0x7F, HABIT_STORE_ENCODED_SIZE },
        { "槽数不符", 6, 0x0B, HABIT_STORE_ENCODED_SIZE },
        { "长度不足", 0, 0x00, HABIT_STORE_ENCODED_SIZE - 1 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t buf[HABIT_STORE_ENCODED_SIZE];
        memcpy(buf, good, sizeof(buf));
        buf[cases[i].offset] = cases[i].value;

        memset(&out, 0, sizeof(out));
        out.today = 999;
        assert(!habit_store_decode(buf, cases[i].len, &out));
        assert(out.today == 999);  // 失败时保持原样
    }

    assert(!habit_store_decode(NULL, sizeof(good), &out));
    assert(!habit_store_decode(good, sizeof(good), NULL));
}

static void test_version_guard_survives_layout_growth(void)
{
    habit_records_t records;
    habit_records_clear(&records);

    uint8_t buf[HABIT_STORE_ENCODED_SIZE];
    assert(habit_store_encode(&records, DAY, 0, buf, sizeof(buf)) == sizeof(buf));

    // 模拟"固件升级后版本号变了但 Flash 里还是旧数据":必须拒绝,
    // 由调用方回落到默认值,而不是按新语义解释旧布局。
    buf[4] = (uint8_t)(HABIT_STORE_VERSION + 1);
    habit_store_data_t out;
    memset(&out, 0, sizeof(out));
    assert(!habit_store_decode(buf, sizeof(buf), &out));
}

int main(void)
{
    test_encoded_size_is_fixed();
    test_round_trip_preserves_records();
    test_empty_records_round_trip();
    test_encode_rejects_small_buffer();
    test_decode_rejects_corrupt_input();
    test_version_guard_survives_layout_growth();
    return 0;
}