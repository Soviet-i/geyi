#include "geyi_history.h"
#include <string.h>

/* 固定 10 槽环形缓冲：写满后覆盖最旧（REQ-603）。 */
static GyRecord items[GY_HISTORY_CAPACITY];
static int count;
static int next;   /* 下一条写入槽位 */

void gy_history_reset(void)
{
    memset(items, 0, sizeof(items));
    count = 0;
    next = 0;
}

int gy_history_add(const GyRecord *record)
{
    int slot;
    if (!record) return -1;
    if (record->outcome < GY_RED_WIN || record->outcome > GY_DRAW) return -1;
    if (record->ended_by > GY_END_BLUE_RESIGN) return -1;
    if (record->winner > 2u) return -1;
    if (record->mode > 2u) return -1;
    if (record->size < 4u || record->size > (uint8_t)GY_MAX) return -1;
    slot = next;
    items[slot] = *record;
    items[slot].version = (uint16_t)GY_HISTORY_VERSION;
    next = (next + 1) % GY_HISTORY_CAPACITY;
    if (count < GY_HISTORY_CAPACITY) ++count;
    return slot;
}

int gy_history_count(void)
{
    return count;
}

const GyRecord *gy_history_at(int index)
{
    if (index < 0 || index >= count) return NULL;
    return &items[(next - 1 - index + GY_HISTORY_CAPACITY * 2) % GY_HISTORY_CAPACITY];
}

void gy_history_clear(void)
{
    count = 0;
    next = 0;
    memset(items, 0, sizeof(items));
}

int gy_history_selftest(void)
{
    GyRecord record;
    int i;

    gy_history_reset();
    if (gy_history_count() != 0 || gy_history_at(0) != NULL || gy_history_at(-1) != NULL) return 0;

    memset(&record, 0, sizeof(record));
    record.mode = 0;
    record.size = 6;
    record.outcome = GY_RED_WIN;
    record.winner = 0;
    record.ended_by = GY_END_KNOCKOUT;
    record.moves = 9;
    record.duration_ms = 65000u;
    record.ts_sec = 12u;
    if (gy_history_add(&record) < 0) return 0;
    if (gy_history_count() != 1) return 0;
    if (!gy_history_at(0) || gy_history_at(0)->moves != 9) return 0;
    if (gy_history_at(0)->version != GY_HISTORY_VERSION) return 0;
    if (gy_history_at(1) != NULL) return 0;

    /* 非法记录必须拒绝且不覆盖已有数据。 */
    record.outcome = GY_PLAYING;
    if (gy_history_add(&record) != -1 || gy_history_count() != 1) return 0;
    record.outcome = GY_DRAW;
    record.size = 3;
    if (gy_history_add(&record) != -1 || gy_history_count() != 1) return 0;
    record.size = 6;
    record.ended_by = 7;
    if (gy_history_add(&record) != -1 || gy_history_count() != 1) return 0;

    /* 满 10 条后覆盖最旧，最新仍在 index 0。 */
    gy_history_reset();
    for (i = 0; i < GY_HISTORY_CAPACITY + 2; ++i) {
        memset(&record, 0, sizeof(record));
        record.mode = 0;
        record.size = 6;
        record.outcome = (uint8_t)(i % 2 ? GY_BLUE_WIN : GY_RED_WIN);
        record.winner = (uint8_t)(i % 2);
        record.ended_by = GY_END_KNOCKOUT;
        record.moves = (uint16_t)i;
        record.duration_ms = (uint32_t)i * 1000u;
        if (gy_history_add(&record) < 0) return 0;
    }
    if (gy_history_count() != GY_HISTORY_CAPACITY) return 0;
    if (!gy_history_at(0) || gy_history_at(0)->moves != GY_HISTORY_CAPACITY + 1) return 0;
    if (!gy_history_at(GY_HISTORY_CAPACITY - 1) ||
        gy_history_at(GY_HISTORY_CAPACITY - 1)->moves != 2) return 0;
    if (gy_history_at(GY_HISTORY_CAPACITY) != NULL) return 0;
    for (i = 1; i < GY_HISTORY_CAPACITY; ++i)
        if (gy_history_at(i - 1)->moves != gy_history_at(i)->moves + 1) return 0;

    gy_history_clear();
    if (gy_history_count() != 0 || gy_history_at(0) != NULL) return 0;
    gy_history_clear();   /* 重复清空不崩溃 */
    return gy_history_count() == 0;
}
