// main/habit_store.h —— NVS 记录的编码/解码。纯逻辑,不调用 NVS。
//
// 为什么不用 memcpy 直接存结构体:
//   结构体的 padding 和字段布局是编译器的实现细节,写进 Flash 后就成了
//   "隐式格式"。一旦固件升级改变了布局(加字段、换对齐),旧数据会被按新
//   语义解释,产生难以察觉的损坏。这里改成逐字段小端序编码,格式显式且稳定。
//
// 解码端采取"宁可丢弃也不猜"的策略:magic/版本/槽数/长度任何一项不符即拒绝,
// 由调用方回落到"无记录 + 请用户确认日期",而不是把可疑数据当成真实记录。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "habit_model.h"

// 布局版本。字段含义或数量变化时必须自增。
#define HABIT_STORE_VERSION 1u
#define HABIT_STORE_MAGIC 0x314B4248u  /* "HBK1" */

typedef struct {
    uint16_t version;
    uint16_t slots;
    int32_t today;      // 写入时的逻辑日
    int64_t wall_sec;   // 写入时的墙钟
    habit_records_t records;
} habit_store_data_t;

// 编码后的固定长度:4 magic + 2 version + 2 slots + 4 today + 8 wall + 每槽 5 字节。
#define HABIT_STORE_ENCODED_SIZE \
    (20u + (size_t)HABIT_RECORD_SLOTS * 5u)

// 写入 buf,返回写入字节数;缓冲区不足返回 0(不写任何部分数据)。
size_t habit_store_encode(const habit_records_t *records, int32_t today, int64_t wall_sec,
                          uint8_t *buf, size_t buf_len);

// 解码。任何校验不通过都返回 false,且不改动 out。
bool habit_store_decode(const uint8_t *buf, size_t len, habit_store_data_t *out);