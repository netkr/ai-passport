#include "habit_store.h"

#include <string.h>

// 显式小端序编解码。读有符号数用 memcpy 而不是强转:uint→int 的越界转换在
// C 标准里是实现定义(二补码之外的解释都合法),用位模式拷贝则没有歧义。
static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static void put_i32(uint8_t *p, int32_t value)
{
    put_u32(p, (uint32_t)value);
}

static void put_i64(uint8_t *p, int64_t value)
{
    const uint64_t raw = (uint64_t)value;
    for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(raw >> (8 * i));
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; i++) value |= (uint32_t)p[i] << (8 * i);
    return value;
}

static int32_t get_i32(const uint8_t *p)
{
    const uint32_t raw = get_u32(p);
    int32_t value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static int64_t get_i64(const uint8_t *p)
{
    uint64_t raw = 0;
    for (unsigned i = 0; i < 8; i++) raw |= (uint64_t)p[i] << (8 * i);
    int64_t value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

size_t habit_store_encode(const habit_records_t *records, int32_t today, int64_t wall_sec,
                          uint8_t *buf, size_t buf_len)
{
    if (records == NULL || buf == NULL || buf_len < HABIT_STORE_ENCODED_SIZE) return 0;

    put_u32(buf + 0, HABIT_STORE_MAGIC);
    put_u16(buf + 4, (uint16_t)HABIT_STORE_VERSION);
    put_u16(buf + 6, (uint16_t)HABIT_RECORD_SLOTS);
    put_i32(buf + 8, today);
    put_i64(buf + 12, wall_sec);

    size_t offset = 20;
    for (size_t i = 0; i < HABIT_RECORD_SLOTS; i++) {
        put_i32(buf + offset, records->slots[i].day);
        buf[offset + 4] = records->slots[i].mask;
        offset += 5;
    }
    return offset;
}

bool habit_store_decode(const uint8_t *buf, size_t len, habit_store_data_t *out)
{
    if (buf == NULL || out == NULL) return false;
    if (len < HABIT_STORE_ENCODED_SIZE) return false;
    if (get_u32(buf + 0) != HABIT_STORE_MAGIC) return false;
    if (get_u16(buf + 4) != (uint16_t)HABIT_STORE_VERSION) return false;
    if (get_u16(buf + 6) != (uint16_t)HABIT_RECORD_SLOTS) return false;

    // 全部校验通过后才写 out:失败时调用方手里的数据保持原样,不会掺进半截结果。
    out->version = (uint16_t)HABIT_STORE_VERSION;
    out->slots = (uint16_t)HABIT_RECORD_SLOTS;
    out->today = get_i32(buf + 8);
    out->wall_sec = get_i64(buf + 12);

    size_t offset = 20;
    for (size_t i = 0; i < HABIT_RECORD_SLOTS; i++) {
        out->records.slots[i].day = get_i32(buf + offset);
        out->records.slots[i].mask = buf[offset + 4];
        offset += 5;
    }
    return true;
}