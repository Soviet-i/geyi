#ifndef GEYI_HISTORY_H
#define GEYI_HISTORY_H
#include <stdint.h>
#include "geyi_game.h"

/* 最近对局历史（REQ-601~REQ-606）：RAM 环形缓冲 + 可持久化快照。
 * 本模块保持纯逻辑，不依赖 LVGL/RT-Thread/FlashDB；板端存储适配器在
 * geyi_history_store.c。v2 记录增加胜方身份（AI/真人/机主/对手）。 */
#define GY_HISTORY_CAPACITY 10
#define GY_HISTORY_VERSION  2u
#define GY_HISTORY_STORE_MAGIC   0x47594852u /* 'GYHR' */
#define GY_HISTORY_STORE_VERSION 1u

/* 结束方式 */
enum { GY_END_KNOCKOUT, GY_END_RED_RESIGN, GY_END_BLUE_RESIGN, GY_END_DISCONNECT };

/* 胜方身份：单机留空；人机 AI/真人；联机本机/远端。 */
enum {
    GY_IDENTITY_NONE = 0,
    GY_IDENTITY_AI,
    GY_IDENTITY_HUMAN,
    GY_IDENTITY_OWNER,
    GY_IDENTITY_OPPONENT
};

typedef struct {
    uint16_t version;      /* GY_HISTORY_VERSION */
    uint8_t  mode;         /* 0 单机 / 1 人机 / 2 联机，对应 GY_MODE_* */
    uint8_t  size;         /* 棋盘边长 */
    uint8_t  outcome;      /* GY_RED_WIN / GY_BLUE_WIN / GY_DRAW */
    uint8_t  winner;       /* 0 红 / 1 蓝 / 2 无（平局） */
    uint8_t  ended_by;     /* GY_END_* */
    uint8_t  winner_identity; /* GY_IDENTITY_*，替代 v1 的 reserved */
    uint16_t moves;        /* 本局提交的回合数 */
    uint32_t duration_ms;  /* 单局时长，来自 gy_timer */
    uint32_t ts_sec;       /* 记录时刻（开机秒数；本板无 RTC） */
} GyRecord;

/* 持久化快照：固定大小、无指针；由 store 适配器整体写入 FlashDB。 */
typedef struct {
    uint32_t magic;
    uint16_t store_version;
    uint16_t record_version;
    uint8_t  count;
    uint8_t  next;
    uint8_t  reserved[2];
    GyRecord items[GY_HISTORY_CAPACITY];
} GyHistoryStore;

void gy_history_reset(void);
/* 写入一条记录；index 0 恒为最新。非法字段返回 -1，不改变已有数据。 */
int gy_history_add(const GyRecord *record);
int gy_history_count(void);
const GyRecord *gy_history_at(int index);
/* 清空（REQ-606）；空历史再次清空也返回 0，不崩溃。只改 RAM 并置 dirty。 */
void gy_history_clear(void);
int gy_history_selftest(void);

/* RAM 与持久化快照之间的纯逻辑转换；0 成功，-1 失败。 */
int gy_history_store_pack(GyHistoryStore *out);
int gy_history_store_unpack(const GyHistoryStore *in);
int gy_history_dirty(void);
void gy_history_mark_clean(void);

/* 纯映射函数：供 UI 写记录、主机测试复用。 */
uint8_t gy_history_identity_pve(int outcome, int ai_side);
uint8_t gy_history_identity_pvp(int outcome, int local_color);
uint8_t gy_history_ended_by_resign(int winner);

#endif
