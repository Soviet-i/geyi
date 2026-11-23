#include "geyi_history.h"
#include "geyi_config.h"
#include <string.h>

/* 固定 10 槽环形缓冲：写满后覆盖最旧（REQ-603）。 */
static GyRecord items[GY_HISTORY_CAPACITY];
static int count;
static int next;      /* 下一条写入槽位 */
static int dirty;     /* RAM 与持久化快照不一致，由 store 适配器消费 */

static int record_valid(const GyRecord *r)
{
    int winner_color;
    if (!r || r->version != GY_HISTORY_VERSION) return 0;
    if (r->outcome < GY_RED_WIN || r->outcome > GY_DRAW) return 0;
    if (r->winner > 2u) return 0;
    if (r->ended_by > GY_END_DISCONNECT) return 0;
    if (r->mode > GY_MODE_PVP) return 0;
    if (r->size < GY_SIZE_MIN || r->size > GY_SIZE_MAX) return 0;
    winner_color = r->outcome == GY_RED_WIN ? 0 : r->outcome == GY_BLUE_WIN ? 1 : 2;
    if (r->winner != (uint8_t)winner_color) return 0;
    if (r->winner_identity > GY_IDENTITY_OPPONENT) return 0;
    if (r->mode == GY_MODE_SINGLE && r->winner_identity != GY_IDENTITY_NONE) return 0;
    if (r->mode == GY_MODE_PVE &&
        r->winner_identity != GY_IDENTITY_NONE &&
        r->winner_identity != GY_IDENTITY_AI &&
        r->winner_identity != GY_IDENTITY_HUMAN) return 0;
    if (r->mode == GY_MODE_PVP &&
        r->winner_identity != GY_IDENTITY_NONE &&
        r->winner_identity != GY_IDENTITY_OWNER &&
        r->winner_identity != GY_IDENTITY_OPPONENT) return 0;
    return 1;
}

/* 只写 RAM，不置 dirty；供 add 和 load 复用。 */
static int add_ram(const GyRecord *record)
{
    GyRecord copy;
    int slot;
    if (!record) return -1;
    copy = *record;
    copy.version = (uint16_t)GY_HISTORY_VERSION;
    if (!record_valid(&copy)) return -1;
    slot = next;
    items[slot] = copy;
    next = (next + 1) % GY_HISTORY_CAPACITY;
    if (count < GY_HISTORY_CAPACITY) ++count;
    return slot;
}

void gy_history_reset(void)
{
    memset(items, 0, sizeof(items));
    count = 0;
    next = 0;
    dirty = 0;
}

int gy_history_add(const GyRecord *record)
{
    int slot = add_ram(record);
    if (slot >= 0) dirty = 1;
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
    dirty = 1;
}

int gy_history_dirty(void)
{
    return dirty;
}

void gy_history_mark_clean(void)
{
    dirty = 0;
}

int gy_history_store_pack(GyHistoryStore *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->magic = GY_HISTORY_STORE_MAGIC;
    out->store_version = (uint16_t)GY_HISTORY_STORE_VERSION;
    out->record_version = (uint16_t)GY_HISTORY_VERSION;
    out->count = (uint8_t)count;
    out->next = (uint8_t)next;
    memcpy(out->items, items, sizeof(items));
    return 0;
}

int gy_history_store_unpack(const GyHistoryStore *in)
{
    GyRecord valid[GY_HISTORY_CAPACITY];
    int i, n = 0, start;

    if (!in) return -1;
    if (in->magic != GY_HISTORY_STORE_MAGIC ||
        in->store_version != GY_HISTORY_STORE_VERSION ||
        in->record_version != GY_HISTORY_VERSION) return -1;
    if (in->count > GY_HISTORY_CAPACITY || in->next >= GY_HISTORY_CAPACITY) return -1;
    if (in->count == 0) {
        gy_history_reset();
        return 0;
    }

    start = (in->next - in->count + GY_HISTORY_CAPACITY) % GY_HISTORY_CAPACITY;
    for (i = 0; i < in->count; ++i) {
        const GyRecord *r = &in->items[(start + i) % GY_HISTORY_CAPACITY];
        if (record_valid(r)) valid[n++] = *r;   /* 损坏记录跳过，保留其余（REQ-606） */
    }

    gy_history_reset();
    for (i = 0; i < n; ++i) add_ram(&valid[i]);
    dirty = 0;
    return 0;
}

uint8_t gy_history_identity_pve(int outcome, int ai_side)
{
    int winner_color;
    if (outcome == GY_DRAW || ai_side < 0 || ai_side > 1) return GY_IDENTITY_NONE;
    if (outcome != GY_RED_WIN && outcome != GY_BLUE_WIN) return GY_IDENTITY_NONE;
    winner_color = outcome == GY_RED_WIN ? 0 : 1;
    return winner_color == ai_side ? GY_IDENTITY_AI : GY_IDENTITY_HUMAN;
}

uint8_t gy_history_identity_pvp(int outcome, int local_color)
{
    int winner_color;
    if (outcome == GY_DRAW || local_color < 0 || local_color > 1) return GY_IDENTITY_NONE;
    if (outcome != GY_RED_WIN && outcome != GY_BLUE_WIN) return GY_IDENTITY_NONE;
    winner_color = outcome == GY_RED_WIN ? 0 : 1;
    return winner_color == local_color ? GY_IDENTITY_OWNER : GY_IDENTITY_OPPONENT;
}

uint8_t gy_history_ended_by_resign(int winner)
{
    return winner == GY_RED_WIN ? GY_END_BLUE_RESIGN :
           winner == GY_BLUE_WIN ? GY_END_RED_RESIGN : GY_END_KNOCKOUT;
}

int gy_history_selftest(void)
{
    GyRecord record;
    GyHistoryStore store;
    int i;

    gy_history_reset();
    if (gy_history_count() != 0 || gy_history_at(0) != NULL || gy_history_at(-1) != NULL) return 0;

    memset(&record, 0, sizeof(record));
    record.mode = GY_MODE_SINGLE;
    record.size = 6;
    record.outcome = GY_RED_WIN;
    record.winner = 0;
    record.ended_by = GY_END_KNOCKOUT;
    record.winner_identity = GY_IDENTITY_NONE;
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
    record.ended_by = GY_END_KNOCKOUT;
    record.winner_identity = GY_IDENTITY_AI;   /* 单机不能带 AI 身份 */
    if (gy_history_add(&record) != -1 || gy_history_count() != 1) return 0;
    record.winner_identity = GY_IDENTITY_NONE;
    record.outcome = GY_BLUE_WIN;
    record.winner = 1;
    record.mode = GY_MODE_PVE;
    record.winner_identity = GY_IDENTITY_AI;
    if (gy_history_add(&record) < 0) return 0;
    if (gy_history_count() != 2) return 0;

    /* 满 10 条后覆盖最旧，最新仍在 index 0。 */
    gy_history_reset();
    for (i = 0; i < GY_HISTORY_CAPACITY + 2; ++i) {
        memset(&record, 0, sizeof(record));
        record.mode = GY_MODE_SINGLE;
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

    /* 快照往返；损坏的 magic/version/count 必须拒绝。 */
    if (gy_history_store_pack(&store) < 0) return 0;
    gy_history_reset();
    if (gy_history_store_unpack(&store) < 0) return 0;
    if (gy_history_count() != GY_HISTORY_CAPACITY) return 0;
    if (gy_history_at(0)->moves != GY_HISTORY_CAPACITY + 1) return 0;
    store.magic = 0;
    if (gy_history_store_unpack(&store) != -1) return 0;
    store.magic = GY_HISTORY_STORE_MAGIC;
    store.store_version = 99;
    if (gy_history_store_unpack(&store) != -1) return 0;
    store.store_version = GY_HISTORY_STORE_VERSION;
    store.count = GY_HISTORY_CAPACITY + 1;
    if (gy_history_store_unpack(&store) != -1) return 0;

    /* 身份映射：人机 AI/真人，联机本机/远端，平局留空。 */
    if (gy_history_identity_pve(GY_RED_WIN, 0) != GY_IDENTITY_AI) return 0;
    if (gy_history_identity_pve(GY_RED_WIN, 1) != GY_IDENTITY_HUMAN) return 0;
    if (gy_history_identity_pve(GY_DRAW, 0) != GY_IDENTITY_NONE) return 0;
    if (gy_history_identity_pvp(GY_RED_WIN, 0) != GY_IDENTITY_OWNER) return 0;
    if (gy_history_identity_pvp(GY_BLUE_WIN, 0) != GY_IDENTITY_OPPONENT) return 0;
    if (gy_history_identity_pvp(GY_DRAW, 0) != GY_IDENTITY_NONE) return 0;
    if (gy_history_ended_by_resign(GY_RED_WIN) != GY_END_BLUE_RESIGN) return 0;
    if (gy_history_ended_by_resign(GY_BLUE_WIN) != GY_END_RED_RESIGN) return 0;

    gy_history_clear();
    if (gy_history_count() != 0 || gy_history_at(0) != NULL) return 0;
    gy_history_clear();   /* 重复清空不崩溃 */
    if (gy_history_count() != 0) return 0;
    gy_history_reset();   /* 自检不留下 dirty，避免把测试数据写进 Flash */
    return 1;
}
