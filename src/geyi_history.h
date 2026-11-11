#ifndef GEYI_HISTORY_H
#define GEYI_HISTORY_H
#include <stdint.h>
#include "geyi_game.h"

/* 最近对局历史（REQ-601~REQ-606）：RAM 内环形缓冲、无动态分配、不依赖 LVGL。
 * v1 只在内存里保存最近 N 局，重启清空；持久化见 docs/adr 中的后续任务。 */
#define GY_HISTORY_CAPACITY 10
#define GY_HISTORY_VERSION  1u

/* 结束方式 */
enum { GY_END_KNOCKOUT, GY_END_RED_RESIGN, GY_END_BLUE_RESIGN };

typedef struct {
    uint16_t version;      /* GY_HISTORY_VERSION */
    uint8_t  mode;         /* 0 单机 / 1 人机 / 2 联机，对应 GY_MODE_* */
    uint8_t  size;         /* 棋盘边长 */
    uint8_t  outcome;      /* GY_RED_WIN / GY_BLUE_WIN / GY_DRAW */
    uint8_t  winner;       /* 0 红 / 1 蓝 / 2 无（平局） */
    uint8_t  ended_by;     /* GY_END_* */
    uint8_t  reserved;
    uint16_t moves;        /* 本局提交的回合数 */
    uint32_t duration_ms;  /* 单局时长，来自 gy_timer */
    uint32_t ts_sec;       /* 记录时刻（开机秒数；本板无 RTC） */
} GyRecord;

void gy_history_reset(void);
/* 写入一条记录；index 0 恒为最新。非法字段返回 -1，不改变已有数据。 */
int gy_history_add(const GyRecord *record);
int gy_history_count(void);
const GyRecord *gy_history_at(int index);
/* 清空（REQ-606）；空历史再次清空也返回 0，不崩溃。 */
void gy_history_clear(void);
int gy_history_selftest(void);
#endif
