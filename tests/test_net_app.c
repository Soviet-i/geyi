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

static void feed_host(uint8_t conn, GyNetMsg *m) { host_process(conn, 1, m); }
static void feed_host_gen(uint8_t conn, uint8_t gen, GyNetMsg *m)
{
    host_process(conn, gen, m);
}

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
    memset(&j, 0, sizeof(j));
    j.role = 0;
    j.player_id = 0x2222;
    j.room_id = g.room_id;
    CHECK(gy_net_pack_join_req(&m, &j) == GY_NET_OK);
    feed_host(conn, &m);
    return 0;
}

static int host_join_role(uint8_t conn, uint8_t role, uint16_t player_id)
{
    GyNetJoinReq j;
    GyNetMsg m;
    memset(&j, 0, sizeof(j));
    j.role = role;
    j.player_id = player_id;
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
    CHECK(g.reack_left == 3);   /* 换座 ACK 需要重发，单包丢失可恢复 */
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

static int test_seat_ready_independent(void)
{
    CHECK(setup_host(0x2006) == 0);
    gy_net_room_set_player_id(&g.room, g.room.view.host_color, 0xAAAA);
    CHECK(gy_net_app_seat_ready(GY_NET_COLOR_RED) == 0);
    CHECK(gy_net_app_set_ready(1) == 1);
    CHECK(gy_net_app_seat_ready(GY_NET_COLOR_RED) == 1);
    CHECK(gy_net_app_seat_ready(GY_NET_COLOR_BLUE) == 0);
    CHECK(gy_net_app_host_ready() == 1 && gy_net_app_guest_ready() == 0);

    CHECK(host_join_player(1) == 0);
    CHECK(gy_net_app_seat_id(GY_NET_COLOR_RED) == 0xAAAA);
    CHECK(gy_net_app_seat_id(GY_NET_COLOR_BLUE) == 0x2222);
    CHECK(gy_net_app_seat_ready(GY_NET_COLOR_RED) == 1);
    CHECK(gy_net_app_seat_ready(GY_NET_COLOR_BLUE) == 0);
    CHECK(host_ready(1, 1) == 0);
    CHECK(gy_net_app_seat_ready(GY_NET_COLOR_BLUE) == 1);
    CHECK(gy_net_app_seat_ready(GY_NET_COLOR_RED) == 1);
    return 0;
}

static int test_host_leave_closes_and_client_sees_closed(void)
{
    GyNetRoomState s;
    GyNetMsg m;

    CHECK(setup_host(0x2007) == 0);
    CHECK(host_join_player(1) == 0);
    gy_net_app_leave();
    CHECK(g.active == 0);
    CHECK(g.room.view.room_id == 0);   /* app 层已清空房间，不能继续被接管 */

    /* 客方收到 CLOSED 后不能接管房间，而是提示“房间不存在”。 */
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.room_id = 0x3007;
    g.room_count = 1;
    memset(&g.rooms[0], 0, sizeof(g.rooms[0]));
    g.rooms[0].room_id = g.room_id;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    memset(&s, 0, sizeof(s));
    s.phase = GY_NET_PHASE_CLOSED;
    s.host_color = GY_NET_COLOR_RED;
    s.guest_color = GY_NET_COLOR_BLUE;
    s.room_id = g.room_id;
    s.seq = 3;
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(g.active == 0);
    CHECK(g.join_rejected == 1);
    CHECK(g.join_reason == GY_NET_JOIN_REASON_NOT_FOUND);
    CHECK(g.room_count == 0);
    return 0;
}

static int test_empty_lobby_index_rejected(void)
{
    gy_net_app_init();
    CHECK(gy_net_app_join_index(0) == -1);
    CHECK(g.join_rejected == 1);
    CHECK(g.join_reason == GY_NET_JOIN_REASON_NOT_FOUND);
    return 0;
}

static int test_stale_lobby_room_rejected(void)
{
    gy_net_app_init();
    g.now = 20000;
    g.room_count = 1;
    memset(&g.rooms[0], 0, sizeof(g.rooms[0]));
    g.rooms[0].room_id = 0x3008;
    g.rooms[0].players = 1;
    g.rooms[0].seen_ms = 1000;   /* 19s 前的摘要 */
    CHECK(gy_net_app_join_index(0) == -1);
    CHECK(g.join_rejected == 1);
    CHECK(g.join_reason == GY_NET_JOIN_REASON_NOT_FOUND);
    CHECK(g.room_count == 0);
    return 0;
}

static int test_full_player_join_downgrades_to_spectator(void)
{
    CHECK(setup_host(0x2009) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(host_join_player(2) == 0);   /* 房间已满：降级为旁观 */
    CHECK(g.room.view.player_count == 2);
    CHECK(g.room.view.spectator_count == 1);
    CHECK(g.conns[1].used == 1);
    CHECK(g.conns[1].role == GY_NET_ROLE_SPECTATOR);
    return 0;
}

static int test_auto_role_uses_actual_space(void)
{
    CHECK(setup_host(0x200A) == 0);
    CHECK(host_join_role(1, 2, 0x3333) == 0);   /* auto：1/2 -> 玩家 */
    CHECK(g.room.view.player_count == 2);
    CHECK(g.conns[0].role == GY_NET_ROLE_PLAYER);
    CHECK(host_join_role(2, 2, 0x4444) == 0);   /* auto：满员 -> 旁观 */
    CHECK(g.room.view.player_count == 2);
    CHECK(g.room.view.spectator_count == 1);
    CHECK(g.conns[1].role == GY_NET_ROLE_SPECTATOR);
    return 0;
}

static int test_guest_takeover_when_state_lost(void)
{
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    g.token = 0x7777;
    g.local_player_id = 0x2222;
    g.room_id = 0x200B;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    g.room.view.host_color = GY_NET_COLOR_RED;
    g.room.view.guest_color = GY_NET_COLOR_BLUE;
    g.room.view.player_count = 2;
    g.room.view.phase = GY_NET_PHASE_WAIT;
    g.dropped = 1;
    g.drop_at = 0;
    g.last_rx = 0;
    /* 链路仍通时不能接管：可能是房主只是慢，接管会变成双房主。 */
    g.link_down = 0;
    g.now = 3000;
    gy_net_app_tick(3000);
    CHECK(g.active == 2);
    CHECK(g.role == GY_NET_ROLE_PLAYER);
    /* 链路确实断开超过 2.5s 才允许接管。 */
    g.link_down = 1;
    g.now = 3001;
    gy_net_app_tick(3001);
    CHECK(g.active == 1);
    CHECK(g.role == GY_NET_ROLE_HOST);
    CHECK(g.local_color == GY_NET_COLOR_BLUE);
    CHECK(gy_net_app_seat_id(GY_NET_COLOR_BLUE) == 0x2222);
    CHECK(g.room.view.player_count == 1);
    return 0;
}

static int test_rejoin_reject_starts_fresh_join(void)
{
    GyNetReject r;
    GyNetMsg m;
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    g.token = 0x9999;
    g.room_id = 0x200D;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    g.room.view.phase = GY_NET_PHASE_WAIT;
    g.room.view.player_count = 2;
    g.dropped = 1;
    g.link_down = 0;
    g.now = 1000;
    memset(&r, 0, sizeof(r));
    r.reason = (uint8_t)(-GY_NET_ERR_TOKEN);
    CHECK(gy_net_pack_reject(&m, &r) == GY_NET_OK);
    client_process(&m);
    CHECK(g.pending_join == 1);
    CHECK(g.join_req_role == 2);          /* auto */
    CHECK(g.local_color == GY_NET_COLOR_NONE);
    CHECK(g.dropped == 0);
    CHECK(g.link_down == 0);              /* 链路仍通，不清成断线 */
    return 0;
}

static int test_released_conn_cannot_act(void)
{
    uint32_t token;
    CHECK(setup_host(0x200C) == 0);
    CHECK(host_join_player(1) == 0);
    token = g.conns[0].token;
    CHECK(gy_net_room_disconnect_token(&g.room, token, 0) == GY_NET_OK);
    CHECK(g.room.view.player_count == 2);          /* 保留 10s 等重连 */
    CHECK(g.room.seats[GY_NET_COLOR_BLUE].connected == 0);
    /* 旧连接还活着但席位已断线：READY 不能落到任何座位。 */
    CHECK(host_ready(1, 1) == 0);
    CHECK(g.room.view.host_ready == 0);
    CHECK(g.room.view.guest_ready == 0);
    return 0;
}

static int test_client_ready_syncs_from_room_state(void)
{
    GyNetRoomState s;
    GyNetMsg m;
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    g.local_ready = 1;
    g.room_id = 0x200F;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    memset(&s, 0, sizeof(s));
    s.phase = GY_NET_PHASE_PLAYING;
    s.host_color = GY_NET_COLOR_RED;
    s.guest_color = GY_NET_COLOR_BLUE;
    s.player_count = 2;
    s.room_id = g.room_id;
    s.seq = 9;
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    /* 开局房主清零 ready；客户端不能保留上一局的乐观值。 */
    CHECK(g.local_ready == 0);
    return 0;
}

static int test_resign_keeps_conn_and_room_loop(void)
{
    GyNetLeave l;
    GyNetMsg m;
    CHECK(setup_host(0x2010) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(host_ready(1, 1) == 0);
    CHECK(host_starts() == 0);
    g.now = 3100;
    memset(&l, 0, sizeof(l));
    l.reason = GY_NET_REASON_RESIGN;
    CHECK(gy_net_pack_leave(&m, &l) == GY_NET_OK);
    feed_host(1, &m);
    CHECK(g.room.view.phase == GY_NET_PHASE_RESULT);
    /* 仅认输不能把客人的 ConnMap 丢掉，否则结算后 READY 收不到。 */
    CHECK(g.conns[0].used == 1);
    gy_net_app_tick(3100 + GY_NET_RESULT_KEEP_MS);
    CHECK(g.room.view.phase == GY_NET_PHASE_WAIT);
    CHECK(g.room.view.guest_ready == 0);
    CHECK(host_ready(1, 1) == 0);
    CHECK(g.room.view.guest_ready == 1);   /* 房主收到了客人的 READY */
    return 0;
}

static int test_leave_after_resign_releases_seat(void)
{
    GyNetLeave l;
    GyNetMsg m;
    CHECK(setup_host(0x2011) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(host_ready(1, 1) == 0);
    CHECK(host_starts() == 0);
    g.now = 3200;
    memset(&l, 0, sizeof(l));
    l.reason = GY_NET_REASON_LEFT;
    CHECK(gy_net_pack_leave(&m, &l) == GY_NET_OK);
    feed_host(1, &m);
    CHECK(g.room.view.phase == GY_NET_PHASE_RESULT);
    CHECK(g.room.view.player_count == 1);
    CHECK(g.conns[0].used == 0);
    return 0;
}

static int test_ignore_state_from_other_room(void)
{
    GyNetRoomState s;
    GyNetMsg m;
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    g.room_id = 0x2013;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    memset(&s, 0, sizeof(s));
    s.phase = GY_NET_PHASE_STARTING;
    s.host_color = GY_NET_COLOR_RED;
    s.guest_color = GY_NET_COLOR_BLUE;
    s.player_count = 2;
    s.room_id = 0x9999;      /* 上一房间的迟到状态 */
    s.seq = 99;
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(g.room.view.phase == GY_NET_PHASE_WAIT);
    CHECK(g.room.view.room_id == 0x2013);
    CHECK(g.room.view.seq != 99);
    return 0;
}

static int test_client_countdown_from_starting_state(void)
{
    GyNetRoomState s;
    GyNetMsg m;
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    g.room_id = 0x2012;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    memset(&s, 0, sizeof(s));
    s.phase = GY_NET_PHASE_STARTING;
    s.host_color = GY_NET_COLOR_RED;
    s.guest_color = GY_NET_COLOR_BLUE;
    s.player_count = 2;
    s.host_ready = 1;
    s.guest_ready = 1;
    s.room_id = g.room_id;
    s.seq = 4;
    g.now = 1000;
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(g.room.view.phase == GY_NET_PHASE_STARTING);
    CHECK(g.room.view.phase_since_ms == 1000);
    CHECK(gy_net_app_countdown_seconds() == 3);
    g.now = 2000;
    CHECK(gy_net_app_countdown_seconds() == 2);
    g.now = 3000;
    CHECK(gy_net_app_countdown_seconds() == 1);
    return 0;
}

static int test_countdown_seconds(void)
{
    CHECK(setup_host(0x200E) == 0);
    g.room.view.phase = GY_NET_PHASE_STARTING;
    g.room.view.phase_since_ms = 1000;
    g.now = 1000;
    CHECK(gy_net_app_countdown_seconds() == 3);
    g.now = 2000;
    CHECK(gy_net_app_countdown_seconds() == 2);
    g.now = 3000;
    CHECK(gy_net_app_countdown_seconds() == 1);
    g.now = 4000;
    CHECK(gy_net_app_countdown_seconds() == 1);
    g.room.view.phase = GY_NET_PHASE_WAIT;
    CHECK(gy_net_app_countdown_seconds() == 0);
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
    g.rooms[0].flags = 1;   /* adv 标记有玩家席位等待重连 */
    rc = gy_net_app_join_index(0);
    /* 摘要满员但可能有“玩家掉线保留 10s”；仍发 auto JOIN 让房主
     * 决定回收，不能客户端直接判死。 */
    CHECK(rc == 0);
    CHECK(g.active == 2 && g.pending_join == 1 && g.join_req_role == 2);

    gy_net_app_init();
    g.room_count = 1;
    memset(&g.rooms[0], 0, sizeof(g.rooms[0]));
    g.rooms[0].room_id = 0x3003;
    g.rooms[0].players = 2;
    g.rooms[0].spectators = 2;
    g.rooms[0].max_spectators = 2;
    g.rooms[0].phase = GY_NET_PHASE_WAIT;
    g.rooms[0].flags = 0;   /* 真满员：客户端立即提示 */
    CHECK(gy_net_app_join_index(0) == -1);
    CHECK(g.join_rejected == 1 && g.join_reason == GY_NET_JOIN_REASON_FULL);

    gy_net_app_init();
    g.room_count = 1;
    memset(&g.rooms[0], 0, sizeof(g.rooms[0]));
    g.rooms[0].room_id = 0x3002;
    g.rooms[0].players = 2;
    g.rooms[0].phase = GY_NET_PHASE_RESULT;   /* 结算中不可加入 */
    rc = gy_net_app_join_index(0);
    CHECK(rc == -1);
    CHECK(g.join_rejected == 1);
    return 0;
}

static int test_leave_clears_spectator_flag(void)
{
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_SPECTATOR;
    CHECK(gy_net_app_is_spectator() == 1);
    gy_net_app_leave();
    CHECK(g.active == 0);
    CHECK(gy_net_app_is_spectator() == 0);
    return 0;
}

static int test_link_gen_reuse_reclaims_seat(void)
{
    GyNetJoinReq j;
    GyNetReady r;
    GyNetMsg m;
    uint32_t old_token;

    CHECK(setup_host(0x2020) == 0);
    CHECK(host_join_player(1) == 0);              /* player_id=0x2222 */
    CHECK(g.room.view.player_count == 2);
    old_token = g.conns[0].token;
    /* 链路断开：WAIT 保留席位 10s，等原玩家重连/重启回收。 */
    host_link_down(1, 1);
    CHECK(g.room.view.player_count == 2);
    CHECK(g.room.seats[GY_NET_COLOR_BLUE].connected == 0);
    CHECK(g.conns[0].used == 0);

    /* 重启后的客方用同一 player_id、新 conn_idx 代际重连：回收原席位。
     * 大厅入口发 auto(2)，也必须回收，不能被当成显式旁观。 */
    memset(&j, 0, sizeof(j));
    j.role = 2;
    j.player_id = 0x2222;
    j.room_id = g.room_id;
    CHECK(gy_net_pack_join_req(&m, &j) == GY_NET_OK);
    feed_host_gen(1, 2, &m);
    CHECK(g.room.view.player_count == 2);
    CHECK(g.conns[0].used == 1 && g.conns[0].gen == 2);
    CHECK(g.conns[0].token != old_token);
    CHECK(g.room.view.guest_id == 0x2222);
    CHECK(g.room.seats[GY_NET_COLOR_BLUE].connected == 1);
    CHECK(g.room.seats[GY_NET_COLOR_BLUE].ready == 0);

    /* 旧代际的迟到 READY 不能操作新客人。 */
    r.ready = 1;
    CHECK(gy_net_pack_ready(&m, &r) == GY_NET_OK);
    feed_host_gen(1, 1, &m);
    CHECK(g.room.view.guest_ready == 0);
    feed_host_gen(1, 2, &m);
    CHECK(g.room.view.guest_ready == 1);
    return 0;
}

static int test_immediate_leave_then_rejoin(void)
{
    GyNetLeave l;
    GyNetMsg m;

    CHECK(setup_host(0x2021) == 0);
    CHECK(host_join_player(1) == 0);
    memset(&l, 0, sizeof(l));
    l.reason = GY_NET_REASON_LEFT;
    CHECK(gy_net_pack_leave(&m, &l) == GY_NET_OK);
    feed_host(1, &m);
    CHECK(g.room.view.player_count == 1);
    CHECK(g.conns[0].used == 0);
    /* 同一 conn_idx 的新客人可以正常加入。 */
    CHECK(host_join_player(1) == 0);
    CHECK(g.room.view.player_count == 2);
    CHECK(g.conns[0].used == 1);
    return 0;
}

static int test_two_guests_one_leaves(void)
{
    GyNetLeave l;
    GyNetMsg m;

    CHECK(setup_host(0x2022) == 0);
    CHECK(host_join_role(1, 0, 0x1111) == 0);   /* 玩家 */
    CHECK(host_join_role(2, 0, 0x2222) == 0);   /* 满员 -> 旁观 */
    CHECK(g.room.view.player_count == 2);
    CHECK(g.room.view.spectator_count == 1);

    memset(&l, 0, sizeof(l));
    l.reason = GY_NET_REASON_LEFT;
    CHECK(gy_net_pack_leave(&m, &l) == GY_NET_OK);
    feed_host(1, &m);
    CHECK(g.room.view.player_count == 1);
    CHECK(g.room.view.spectator_count == 1);

    /* 空出的玩家席位可由第三人补齐；原旁观者不受影响。 */
    CHECK(host_join_role(3, 2, 0x3333) == 0);
    CHECK(g.room.view.player_count == 2);
    CHECK(g.room.view.spectator_count == 1);
    {
        ConnMap *c3 = conn_find(3);
        CHECK(c3 != NULL && c3->role == GY_NET_ROLE_PLAYER);
    }
    return 0;
}

static int test_starting_disconnect_releases_seat(void)
{
    CHECK(setup_host(0x2023) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(gy_net_app_set_ready(1) == 1);
    CHECK(host_ready(1, 1) == 0);
    CHECK(g.room.view.phase == GY_NET_PHASE_STARTING);
    g.now = 1000;
    host_link_down(1, 1);
    gy_net_app_tick(1010);
    CHECK(g.room.view.phase == GY_NET_PHASE_WAIT);
    CHECK(g.room.view.player_count == 2);   /* 保留 10s 等重连 */
    gy_net_app_tick(11001);
    CHECK(g.room.view.player_count == 1);   /* 超时释放 */
    return 0;
}

static int test_side_relation_identity(void)
{
    CHECK(setup_host(0x2025) == 0);
    CHECK(gy_net_app_side_relation(GY_NET_COLOR_RED) == 1);
    CHECK(gy_net_app_side_relation(GY_NET_COLOR_BLUE) == 0);
    CHECK(gy_net_app_side_relation(GY_NET_COLOR_NONE) == -1);

    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_SPECTATOR;
    g.local_color = GY_NET_COLOR_NONE;
    CHECK(gy_net_app_side_relation(GY_NET_COLOR_RED) == -1);
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    CHECK(gy_net_app_side_relation(GY_NET_COLOR_RED) == 0);
    CHECK(gy_net_app_side_relation(GY_NET_COLOR_BLUE) == 1);
    gy_net_app_leave();
    CHECK(gy_net_app_side_relation(GY_NET_COLOR_BLUE) == -1);
    return 0;
}

static int test_expired_seat_not_spectator(void)
{
    GyNetJoinReq j;
    GyNetMsg m;

    CHECK(setup_host(0x2024) == 0);
    CHECK(host_join_player(1) == 0);          /* player_id=0x2222 */
    g.now = 1000;
    host_link_down(1, 1);                     /* WAIT 保留 10s */
    CHECK(g.room.view.player_count == 2);
    /* 10s 已过但还没跑 room tick：JOIN 先到，必须先释放过期席位，
     * 让客人正常成为玩家，而不是被降级旁观。 */
    g.now = 11001;
    memset(&j, 0, sizeof(j));
    j.role = 2;                                /* auto */
    j.player_id = 0x2222;
    j.room_id = g.room_id;
    CHECK(gy_net_pack_join_req(&m, &j) == GY_NET_OK);
    feed_host_gen(1, 2, &m);
    CHECK(g.room.view.player_count == 2);
    CHECK(g.room.view.spectator_count == 0);
    {
        ConnMap *c = conn_find(1);
        CHECK(c != NULL && c->role == GY_NET_ROLE_PLAYER);
    }
    return 0;
}

static int test_spectator_sees_guest_disconnect(void)
{
    GyNetRoom host;
    GyNetRoomState s;
    GyNetMsg m;
    uint32_t token = 0;
    uint8_t color = GY_NET_COLOR_NONE;

    gy_net_room_init_host(&host, 0x2024, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0x2222, 0, &token, &color) ==
          GY_NET_OK);
    gy_net_room_state(&host, &s);
    CHECK(s.seat_online == 0x03);

    /* 模拟旁观者的客户端镜像：只通过 ROOM_STATE 获知座位状态。 */
    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_SPECTATOR;
    g.local_color = GY_NET_COLOR_NONE;
    g.room_id = s.room_id;
    gy_net_room_init_client(&g.room, s.room_id, 0);
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(gy_net_app_seat_online(GY_NET_COLOR_RED) == 1);
    CHECK(gy_net_app_seat_online(GY_NET_COLOR_BLUE) == 1);
    CHECK(gy_net_app_offline_seat() == -1);

    /* 房主感知客席断线并广播：旁观者必须得到蓝方离线/倒计时。 */
    CHECK(gy_net_room_disconnect_token(&host, token, 1000) == GY_NET_OK);
    gy_net_room_state(&host, &s);
    CHECK(s.seat_online == 0x01);
    CHECK(s.seat_reconnect_s[GY_NET_COLOR_BLUE] == 10);
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(gy_net_app_seat_online(GY_NET_COLOR_RED) == 1);
    CHECK(gy_net_app_seat_online(GY_NET_COLOR_BLUE) == 0);
    CHECK(gy_net_app_offline_seat() == GY_NET_COLOR_BLUE);
    CHECK(gy_net_app_seat_reconnect_seconds(GY_NET_COLOR_BLUE) == 10);
    CHECK(gy_net_app_remote_reconnect_seconds() == 10);

    /* 房主 REJOIN 后 seq 必须前进，旁观者才能应用“恢复在线”。 */
    CHECK(gy_net_room_rejoin(&host, token, 0, 5000, &color) == GY_NET_OK);
    gy_net_room_state(&host, &s);
    CHECK(s.seat_online == 0x03);
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(gy_net_app_seat_online(GY_NET_COLOR_BLUE) == 1);
    CHECK(gy_net_app_offline_seat() == -1);
    CHECK(gy_net_app_remote_reconnect_seconds() == 0);
    return 0;
}

static int test_spectator_preview_cleared_on_phase(void)
{
    GyNetRoomState s;
    GyNetSnapshot snap;
    GyGame board;
    GyNetMsg m;

    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_SPECTATOR;
    g.room_id = 0x2025;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    g.preview_valid[GY_NET_COLOR_RED] = 1;
    g.preview_valid[GY_NET_COLOR_BLUE] = 1;
    g.placing = 1;
    g.place_row = 2;
    g.place_col = 2;
    g.pending_move = 1;

    memset(&s, 0, sizeof(s));
    s.phase = GY_NET_PHASE_RESULT;
    s.host_color = GY_NET_COLOR_RED;
    s.guest_color = GY_NET_COLOR_BLUE;
    s.player_count = 2;
    s.room_id = g.room_id;
    s.seq = 1;
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(g.preview_valid[GY_NET_COLOR_RED] == 0);
    CHECK(g.preview_valid[GY_NET_COLOR_BLUE] == 0);

    g.preview_valid[GY_NET_COLOR_RED] = 1;
    g.preview_valid[GY_NET_COLOR_BLUE] = 1;
    memset(&snap, 0, sizeof(snap));
    CHECK(gy_init(&board, gy_default_config(), 1));
    snap.game = board;
    snap.seq = 10;
    CHECK(gy_net_pack_snapshot(&m, &snap) == GY_NET_OK);
    client_process(&m);
    CHECK(g.preview_valid[GY_NET_COLOR_RED] == 0);
    CHECK(g.preview_valid[GY_NET_COLOR_BLUE] == 0);

    /* RESULT 也要清掉放兵/pending，避免结算后残留旧意图。 */
    {
        GyNetResult r;
        g.preview_valid[GY_NET_COLOR_RED] = 1;
        g.placing = 1; g.place_row = 2; g.place_col = 2;
        g.pending_move = 1;
        memset(&r, 0, sizeof(r));
        r.outcome = GY_RED_WIN;
        r.seq = 11;
        CHECK(gy_net_pack_result(&m, &r) == GY_NET_OK);
        client_process(&m);
        CHECK(g.placing == 0 && g.place_row == -1 && g.place_col == -1);
        CHECK(g.pending_move == 0);
        CHECK(g.preview_valid[GY_NET_COLOR_RED] == 0);
    }
    return 0;
}

static int test_starting_countdown_not_reset_by_seq(void)
{
    GyNetRoomState s;
    GyNetMsg m;

    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_BLUE;
    g.room_id = 0x2026;
    gy_net_room_init_client(&g.room, g.room_id, 0);
    memset(&s, 0, sizeof(s));
    s.phase = GY_NET_PHASE_STARTING;
    s.host_color = GY_NET_COLOR_RED;
    s.guest_color = GY_NET_COLOR_BLUE;
    s.player_count = 2;
    s.host_ready = 1;
    s.guest_ready = 1;
    s.room_id = g.room_id;
    s.seq = 1;
    g.now = 1000;
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(g.room.view.phase_since_ms == 1000);
    /* STARTING 中 REJOIN 让 seq++；不能把 3s 倒计时重置为 3。 */
    s.seq = 2;
    g.now = 2000;
    CHECK(gy_net_pack_room_state(&m, &s) == GY_NET_OK);
    client_process(&m);
    CHECK(g.room.view.phase_since_ms == 1000);
    CHECK(gy_net_app_countdown_seconds() == 2);
    return 0;
}

/* 客方会话进入 PLAYING：直接喂一帧权威快照，避免测试依赖 BLE。 */
static int setup_client_playing(uint32_t room_id)
{
    GyGame board;
    GyNetSnapshot snap;
    GyNetMsg m;

    gy_net_app_init();
    g.active = 2;
    g.role = GY_NET_ROLE_PLAYER;
    g.local_color = GY_NET_COLOR_RED;
    g.room_id = room_id;
    gy_net_room_init_client(&g.room, room_id, 0);
    if (!gy_init(&board, gy_default_config(), 1)) return 1;
    board.turn = GY_NET_COLOR_RED;
    memset(&snap, 0, sizeof(snap));
    snap.seq = 1;
    snap.game = board;
    if (gy_net_pack_snapshot(&m, &snap) != GY_NET_OK) return 1;
    client_process(&m);
    return g.room.view.phase == GY_NET_PHASE_PLAYING && g.room.view.game_valid ? 0 : 1;
}

static int test_guest_request_kind_fields(void)
{
    CHECK(setup_client_playing(0x2030) == 0);
    /* 移动请求：只填移动字段；放兵字段的旧值（-1）不参与打包。 */
    g.dir = GY_UP; g.count = 1; g.aim = 0;
    g.placing = 0; g.place_row = -1; g.place_col = -1;
    CHECK(guest_send_move_req() == 1);
    CHECK(g.pending_move == 1);

    /* 放兵请求：只填行/列；方向/数量/方位的旧值不参与打包。 */
    g.pending_move = 0;
    g.placing = 1;
    g.place_row = g.room.view.game.config.size - 1;
    g.place_col = 0;
    g.dir = -1; g.count = -1; g.aim = -128;
    CHECK(guest_send_move_req() == 1);
    CHECK(g.pending_move == 1);
    CHECK(g.placing == 0 && g.place_row == -1 && g.place_col == -1);

    /* 越界移动参数必须拒绝，不能回绕成合法值。 */
    g.pending_move = 0;
    g.placing = 0;
    g.dir = GY_UP; g.count = 1; g.aim = 101;
    CHECK(guest_send_move_req() == 0);
    CHECK(g.pending_move == 0);
    return 0;
}

static int test_snapshot_preserves_placement(void)
{
    GyNetSnapshot snap;
    GyNetMsg m;

    CHECK(setup_client_playing(0x2031) == 0);
    g.placing = 1; g.place_row = 2; g.place_col = 3;
    g.last_preview_valid = 1;
    g.preview_valid[GY_NET_COLOR_RED] = 1;
    g.preview_valid[GY_NET_COLOR_BLUE] = 1;
    g.pending_move = 1;
    g.pending_move_at = 100;

    /* 同 revision 的重同步快照：清 pending，但不能打断放兵/预览。 */
    memset(&snap, 0, sizeof(snap));
    snap.seq = g.room.view.seq + 1;
    snap.game = g.room.view.game;
    CHECK(gy_net_pack_snapshot(&m, &snap) == GY_NET_OK);
    client_process(&m);
    CHECK(g.pending_move == 0);
    CHECK(g.placing == 1 && g.place_row == 2 && g.place_col == 3);
    CHECK(g.last_preview_valid == 1);
    CHECK(g.preview_valid[GY_NET_COLOR_RED] == 1);
    CHECK(g.preview_valid[GY_NET_COLOR_BLUE] == 1);

    /* 棋盘 revision 真的前进：必须清掉本地放兵/预览。 */
    snap.seq++;
    snap.game.revision++;
    CHECK(gy_net_pack_snapshot(&m, &snap) == GY_NET_OK);
    client_process(&m);
    CHECK(g.placing == 0 && g.place_row == -1 && g.place_col == -1);
    CHECK(g.last_preview_valid == 0);
    CHECK(g.preview_valid[GY_NET_COLOR_RED] == 0);
    CHECK(g.preview_valid[GY_NET_COLOR_BLUE] == 0);
    return 0;
}

static int test_preview_clears_placement(void)
{
    CHECK(setup_client_playing(0x2032) == 0);
    g.placing = 1; g.place_row = 1; g.place_col = 1;
    g.last_preview_valid = 0;
    CHECK(gy_net_app_preview(GY_UP, 1, 0) == 1);
    CHECK(g.placing == 0 && g.place_row == -1 && g.place_col == -1);
    return 0;
}

static int test_pending_move_timeout(void)
{
    CHECK(setup_client_playing(0x2033) == 0);
    g.pending_move = 1;
    g.pending_move_at = 100;
    gy_net_app_tick(1099);
    CHECK(g.pending_move == 1);
    gy_net_app_tick(1100);
    CHECK(g.pending_move == 0);
    return 0;
}

static int test_rejoin_wait_does_not_send_stale_snapshot(void)
{
    GyNetRejoin rj;
    GyNetMsg m;
    uint32_t token;
    int i, saw_snapshot = 0;

    CHECK(setup_host(0x2034) == 0);
    CHECK(host_join_player(1) == 0);
    CHECK(host_ready(1, 1) == 0);
    CHECK(host_starts() == 0);
    token = g.room.seats[GY_NET_COLOR_BLUE].token;
    CHECK(token != 0);

    /* 模拟结算后回到 WAIT：棋盘已作废，但 view.game 仍是终局棋盘。 */
    g.room.view.phase = GY_NET_PHASE_WAIT;
    g.room.view.game_valid = 0;
    g.room.view.game.outcome = GY_RED_WIN;
    CHECK(gy_net_room_disconnect_token(&g.room, token, 1000) == GY_NET_OK);

    app_test_tx_count = 0;
    memset(&rj, 0, sizeof(rj));
    rj.token = token;
    rj.last_seq = g.room.view.seq;
    CHECK(gy_net_pack_rejoin(&m, &rj) == GY_NET_OK);
    feed_host(1, &m);
    for (i = 0; i < app_test_tx_count; ++i)
        if (app_test_tx[i] == GY_NET_SNAPSHOT) saw_snapshot = 1;
    CHECK(!saw_snapshot);
    CHECK(g.room.seats[GY_NET_COLOR_BLUE].connected == 1);
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
    if (test_seat_ready_independent()) return 7;
    if (test_host_leave_closes_and_client_sees_closed()) return 8;
    if (test_stale_lobby_room_rejected()) return 9;
    if (test_full_player_join_downgrades_to_spectator()) return 10;
    if (test_auto_role_uses_actual_space()) return 11;
    if (test_guest_takeover_when_state_lost()) return 12;
    if (test_rejoin_reject_starts_fresh_join()) return 15;
    if (test_released_conn_cannot_act()) return 13;
    if (test_empty_lobby_index_rejected()) return 14;
    if (test_client_ready_syncs_from_room_state()) return 17;
    if (test_countdown_seconds()) return 16;
    if (test_resign_keeps_conn_and_room_loop()) return 18;
    if (test_leave_after_resign_releases_seat()) return 19;
    if (test_client_countdown_from_starting_state()) return 20;
    if (test_ignore_state_from_other_room()) return 21;
    if (test_leave_clears_spectator_flag()) return 22;
    if (test_link_gen_reuse_reclaims_seat()) return 23;
    if (test_immediate_leave_then_rejoin()) return 24;
    if (test_two_guests_one_leaves()) return 25;
    if (test_starting_disconnect_releases_seat()) return 26;
    if (test_expired_seat_not_spectator()) return 28;
    if (test_side_relation_identity()) return 29;
    if (test_spectator_sees_guest_disconnect()) return 30;
    if (test_spectator_preview_cleared_on_phase()) return 31;
    if (test_starting_countdown_not_reset_by_seq()) return 32;
    if (test_guest_request_kind_fields()) return 33;
    if (test_snapshot_preserves_placement()) return 34;
    if (test_preview_clears_placement()) return 35;
    if (test_pending_move_timeout()) return 36;
    if (test_rejoin_wait_does_not_send_stale_snapshot()) return 37;
    printf("geyi-net-app-tests: %d checks PASS\n", checks);
    return 0;
}
