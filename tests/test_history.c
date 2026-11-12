#include "geyi_history.h"
#include "geyi_timer.h"
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static GyRecord make(int mode, int size, int outcome, int ended_by, int moves, uint32_t ms)
{
    GyRecord r;
    memset(&r, 0, sizeof(r));
    r.mode = (uint8_t)mode;
    r.size = (uint8_t)size;
    r.outcome = (uint8_t)outcome;
    r.winner = (uint8_t)(outcome == GY_RED_WIN ? 0 : outcome == GY_BLUE_WIN ? 1 : 2);
    r.ended_by = (uint8_t)ended_by;
    r.moves = (uint16_t)moves;
    r.duration_ms = ms;
    r.ts_sec = 42u;
    return r;
}

int main(void)
{
    GyRecord r;
    GyHistoryStore store;
    GyTimer timer;
    int i;

    CHECK(gy_history_selftest());
    CHECK(gy_timer_selftest());

    /* REQ-601/REQ-603：空库、追加、最新在前、容量 10。 */
    gy_history_reset();
    CHECK(gy_history_count() == 0);
    CHECK(gy_history_at(0) == NULL);

    r = make(0, 6, GY_RED_WIN, GY_END_KNOCKOUT, 12, 65000u);
    CHECK(gy_history_add(&r) >= 0);
    CHECK(gy_history_count() == 1);
    CHECK(gy_history_at(0)->moves == 12);
    CHECK(gy_history_at(0)->outcome == GY_RED_WIN);
    CHECK(gy_history_at(0)->duration_ms == 65000u);
    CHECK(gy_history_at(0)->version == GY_HISTORY_VERSION);
    CHECK(gy_history_at(0)->winner_identity == GY_IDENTITY_NONE);

    r = make(1, 6, GY_BLUE_WIN, GY_END_RED_RESIGN, 3, 1000u);
    r.winner_identity = GY_IDENTITY_AI;
    CHECK(gy_history_add(&r) >= 0);
    CHECK(gy_history_at(0)->outcome == GY_BLUE_WIN);
    CHECK(gy_history_at(0)->winner_identity == GY_IDENTITY_AI);
    CHECK(gy_history_at(1)->moves == 12);

    /* REQ-602：非法字段拒绝且不破坏已有记录。 */
    r = make(0, 6, GY_PLAYING, GY_END_KNOCKOUT, 1, 1u);
    CHECK(gy_history_add(&r) == -1);
    CHECK(gy_history_add(NULL) == -1);
    r = make(0, 3, GY_DRAW, GY_END_KNOCKOUT, 1, 1u);   /* 棋盘过小 */
    CHECK(gy_history_add(&r) == -1);
    r = make(0, 6, GY_DRAW, 9, 1, 1u);                 /* 结束方式非法 */
    CHECK(gy_history_add(&r) == -1);
    r = make(0, 6, GY_RED_WIN, GY_END_KNOCKOUT, 1, 1u);
    r.winner_identity = GY_IDENTITY_AI;                 /* 单机不能带身份 */
    CHECK(gy_history_add(&r) == -1);
    CHECK(gy_history_count() == 2);

    /* 满 10 条覆盖最旧。 */
    gy_history_reset();
    for (i = 0; i < GY_HISTORY_CAPACITY + 3; ++i) {
        r = make(i % 2, 6, i % 2 ? GY_BLUE_WIN : GY_RED_WIN, GY_END_KNOCKOUT, i, (uint32_t)i * 1000u);
        CHECK(gy_history_add(&r) >= 0);
        CHECK(gy_history_count() == (i + 1 < GY_HISTORY_CAPACITY ? i + 1 : GY_HISTORY_CAPACITY));
    }
    CHECK(gy_history_at(0)->moves == GY_HISTORY_CAPACITY + 2);
    CHECK(gy_history_at(GY_HISTORY_CAPACITY - 1)->moves == 3);
    CHECK(gy_history_at(GY_HISTORY_CAPACITY) == NULL);

    /* REQ-606：清空幂等。 */
    gy_history_clear();
    CHECK(gy_history_count() == 0);
    gy_history_clear();
    CHECK(gy_history_count() == 0);
    CHECK(gy_history_at(0) == NULL);

    /* REQ-602/REQ-606：持久化快照往返与损坏拒绝。 */
    gy_history_reset();
    r = make(1, 6, GY_RED_WIN, GY_END_KNOCKOUT, 7, 3000u);
    r.winner_identity = GY_IDENTITY_HUMAN;
    CHECK(gy_history_add(&r) >= 0);
    CHECK(gy_history_store_pack(&store) == 0);
    gy_history_reset();
    CHECK(gy_history_store_unpack(&store) == 0);
    CHECK(gy_history_count() == 1);
    CHECK(gy_history_at(0)->winner_identity == GY_IDENTITY_HUMAN);
    store.magic = 0;
    CHECK(gy_history_store_unpack(&store) == -1);
    store.magic = GY_HISTORY_STORE_MAGIC;
    store.store_version = 99;
    CHECK(gy_history_store_unpack(&store) == -1);
    store.store_version = GY_HISTORY_STORE_VERSION;
    store.count = GY_HISTORY_CAPACITY + 1;
    CHECK(gy_history_store_unpack(&store) == -1);

    /* 单条损坏记录跳过，其余保留（REQ-606）。 */
    r = make(0, 6, GY_BLUE_WIN, GY_END_KNOCKOUT, 8, 4000u);
    CHECK(gy_history_add(&r) >= 0);
    CHECK(gy_history_store_pack(&store) == 0);
    store.items[0].version = 0;
    CHECK(gy_history_store_unpack(&store) == 0);
    CHECK(gy_history_count() == 1);

    /* 人机/联机身份与认输方映射。 */
    CHECK(gy_history_identity_pve(GY_RED_WIN, 0) == GY_IDENTITY_AI);
    CHECK(gy_history_identity_pve(GY_RED_WIN, 1) == GY_IDENTITY_HUMAN);
    CHECK(gy_history_identity_pve(GY_DRAW, 0) == GY_IDENTITY_NONE);
    CHECK(gy_history_identity_pvp(GY_RED_WIN, 0) == GY_IDENTITY_OWNER);
    CHECK(gy_history_identity_pvp(GY_BLUE_WIN, 0) == GY_IDENTITY_OPPONENT);
    CHECK(gy_history_identity_pvp(GY_DRAW, 0) == GY_IDENTITY_NONE);
    CHECK(gy_history_ended_by_resign(GY_RED_WIN) == GY_END_BLUE_RESIGN);
    CHECK(gy_history_ended_by_resign(GY_BLUE_WIN) == GY_END_RED_RESIGN);

    /* REQ-604：计时与回绕。 */
    gy_timer_reset(&timer);
    CHECK(!gy_timer_running(&timer));
    CHECK(gy_timer_elapsed(&timer, 1000u) == 0u);
    gy_timer_start(&timer, 1000u);
    CHECK(gy_timer_running(&timer));
    CHECK(gy_timer_elapsed(&timer, 61000u) == 60000u);
    gy_timer_stop(&timer, 61000u);
    CHECK(!gy_timer_running(&timer));
    CHECK(gy_timer_elapsed(&timer, 99999u) == 60000u);   /* 停表后不增长 */
    gy_timer_start(&timer, 0xFFFFFF00u);
    CHECK(gy_timer_elapsed(&timer, 0x00000100u) == 512u); /* uint32 回绕 */

    {   /* 格式化边界 */
        char text[8];
        gy_timer_format(0u, text, (int)sizeof(text));
        CHECK(!strcmp(text, "00:00"));
        gy_timer_format(59999u, text, (int)sizeof(text));
        CHECK(!strcmp(text, "00:59"));
        gy_timer_format(60000u, text, (int)sizeof(text));
        CHECK(!strcmp(text, "01:00"));
        gy_timer_format(6000000u, text, (int)sizeof(text));
        CHECK(!strcmp(text, "99:59"));
    }

    printf("PASS: %d assertions; TC-601~TC-606 history record and match timer\n", checks);
    return 0;
}
