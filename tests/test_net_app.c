/*
 * test_net_app.c —— 联机应用层路由主机测试（TC-504~TC-511 的 app 段）
 *
 * 直接包含 geyi_net_app.c，调用其静态 host_process/client_process，
 * 用内存消息代替 BLE。覆盖：交换红蓝后的 READY/MOVE 颜色、ROOM_STATE 与
 * SNAPSHOT 同 seq、MOVE_REQ 幂等、对局中 LEAVE 判负、满员加入规则。
 * 不依赖 LVGL/RT-Thread/BLE；真实链路仍需双板验证。
 */
#include "../src/geyi_net_app.c"
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static void feed_host(uint8_t conn, GyNetMsg *m) { host_process(conn, m); }

static int setup_host(uint32_t room_id)
{
    g.room_count = 0;
    gy_net_app_init();
    CHECK(gy_net_app_create_room(room_id) == 0);
    return 0;
}

static int host_join_player(uint8_t conn)
{
    GyNetJoinReq j;
    GyNetMsg m;
    j.role = 0;
    j.room_id = g.room_id;
    CHECK(gy_net_pack_join_req(&m, &j) == GY_NET_OK);
    feed_host(conn, &m);
    return 0;
}

static int host_ready(uint8_t conn, int ready)
{
    GyNetReady r;
    GyNetMsg m;
    r.ready = ready ? 1 : 0;
    CHECK(gy_net_pack_ready(&m, &r) == GY_NET_OK);
    feed_host(conn, &m);
    return 0;
}

static int host_starts(void)
{
    CHECK(gy_net_app_set_ready(1) == 1);
    gy_net_app_tick(3000);
    CHECK(gy_net_app_phase() == GY_NET_PHASE_PLAYING);
    return 0;
}

static int first_valid_direction(const GyGame *g)
{
    int d;
    for (d = 0; d < 4; ++d)
        if (gy_eligible_count(g, d) > 0) return d;
    return -1;
}

static int test_swap_ready_and_move_color(void)
{
    CHECK(setup_host(0x2001) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(g.conns[0].used == 1);
    CHECK(gy_net_app_swap() == 1);
    CHECK(g.room.view.host_color == GY_NET_COLOR_BLUE);
    CHECK(g.room.view.guest_color == GY_NET_COLOR_RED);
    CHECK(g.conns[0].color == GY_NET_COLOR_RED);   /* 客方 token 跟到红方 */

    CHECK(host_ready(1, 1) == 0);
    CHECK(g.room.view.guest_ready == 1);           /* READY 必须落到红方客席 */
    CHECK(g.room.view.host_ready == 0);
    CHECK(host_starts() == 0);
    CHECK(g.room.view.game.turn == GY_NET_COLOR_RED); /* 红方先手，客方执红 */
    return 0;
}

static int test_move_req_idempotent(void)
{
    GyNetMoveReq mr;
    GyNetMsg m;
    int d;
    uint32_t rev;
    uint32_t seq;

    CHECK(setup_host(0x2002) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(gy_net_app_swap() == 1);
    CHECK(host_ready(1, 1) == 0);
    CHECK(host_starts() == 0);
    d = first_valid_direction(&g.room.view.game);
    CHECK(d >= 0);
    memset(&mr, 0, sizeof(mr));
    mr.side = GY_NET_COLOR_RED;
    mr.kind = 0;
    mr.direction = (uint8_t)d;
    mr.count = 1;
    mr.seq = 77;
    CHECK(gy_net_pack_move_req(&m, &mr) == GY_NET_OK);
    feed_host(1, &m);
    rev = g.room.view.game.revision;
    seq = g.room.view.seq;
    CHECK(g.room.view.game.turn == GY_NET_COLOR_BLUE);
    feed_host(1, &m);   /* 同一 seq 重复包必须被丢弃 */
    CHECK(g.room.view.game.revision == rev);
    CHECK(g.room.view.seq == seq);
    return 0;
}

static int test_leave_during_play_resigns(void)
{
    GyNetLeave l;
    GyNetMsg m;

    CHECK(setup_host(0x2003) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(gy_net_app_swap() == 1);
    CHECK(host_ready(1, 1) == 0);
    CHECK(host_starts() == 0);
    l.reason = GY_NET_REASON_LEFT;     /* 即使客户端发 LEFT，对局中也按认输兜底 */
    CHECK(gy_net_pack_leave(&m, &l) == GY_NET_OK);
    feed_host(1, &m);
    CHECK(g.room.view.phase == GY_NET_PHASE_RESULT);
    CHECK(g.room.view.result == GY_BLUE_WIN);  /* 红方客席退出，蓝方房主胜 */
    return 0;
}

static int test_state_snapshot_same_seq_client(void)
{
    GyNetRoomState rs;
    GyNetSnapshot snap;
    GyNetMsg m;

    CHECK(setup_host(0x2004) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(host_ready(1, 1) == 0);
    CHECK(host_starts() == 0);
    gy_net_room_state(&g.room, &rs);
    gy_net_room_snapshot(&g.room, &snap);
    CHECK(rs.seq == snap.seq);

    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    g.token = 0x55;
    g.room_id = rs.room_id;
    gy_net_room_init_client(&g.room, rs.room_id, 0);
    CHECK(gy_net_pack_room_state(&m, &rs) == GY_NET_OK);
    client_process(&m);
    CHECK(gy_net_pack_snapshot(&m, &snap) == GY_NET_OK);
    client_process(&m);
    CHECK(g.room.view.game_valid == 1);
    CHECK(g.room.view.phase == GY_NET_PHASE_PLAYING);
    return 0;
}

static int test_duplicate_join_same_conn(void)
{
    int i, used = 0;
    CHECK(setup_host(0x2005) == 0);
    CHECK(host_join_player(1) == 0);
    for (i = 0; i < APP_MAX_CONN; ++i) if (g.conns[i].used) used++;
    CHECK(used == 1);
    CHECK(host_join_player(1) == 0);      /* 重复 JOIN_REQ 不能创建幽灵席位 */
    used = 0;
    for (i = 0; i < APP_MAX_CONN; ++i) if (g.conns[i].used) used++;
    CHECK(used == 1);
    CHECK(g.room.view.player_count == 2);
    return 0;
}

static int test_join_full_lobby_rule(void)
{
    int rc;
    gy_net_app_init();
    g.room_count = 1;
    memset(&g.rooms[0], 0, sizeof(g.rooms[0]));
    g.rooms[0].room_id = 0x3001;
    g.rooms[0].players = 2;
    g.rooms[0].spectators = 2;
    g.rooms[0].max_spectators = 2;
    g.rooms[0].phase = GY_NET_PHASE_WAIT;
    rc = gy_net_app_join_index(0);
    CHECK(rc == -1);
    CHECK(g.join_rejected == 1);
    return 0;
}

int main(void)
{
    if (test_swap_ready_and_move_color()) return 1;
    if (test_move_req_idempotent()) return 2;
    if (test_leave_during_play_resigns()) return 3;
    if (test_state_snapshot_same_seq_client()) return 4;
    if (test_duplicate_join_same_conn()) return 5;
    if (test_join_full_lobby_rule()) return 6;
    printf("geyi-net-app-tests: %d checks PASS\n", checks);
    return 0;
}
