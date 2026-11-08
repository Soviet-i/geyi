/*
 * test_net.c —— 联机纯逻辑主机测试（TC-501~TC-513 的主机段）
 *
 * 运行：python tools/run_host.py
 * 不依赖 LVGL、RT-Thread、BLE 或开发板；只验证协议、房间状态机、大厅和内存管道。
 * 真实 BLE/双板/屏幕必须在物理验证阶段另测，不能用本文件代替。
 */
#include "geyi_net_proto.h"
#include "geyi_net_room.h"
#include "geyi_net_link.h"
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int test_proto_envelope(void)
{
    GyNetMsg in, out;
    uint8_t buf[GY_NET_MAX_PACKET];
    int n;

    gy_net_msg_init(&in, GY_NET_PING, 7);
    n = gy_net_proto_encode(&in, buf, (int)sizeof(buf));
    CHECK(n == GY_NET_HEADER_SIZE);
    CHECK(gy_net_proto_decode(&out, buf, n) == GY_NET_OK);
    CHECK(out.type == GY_NET_PING && out.seq == 7 && out.len == 0);

    CHECK(gy_net_proto_decode(&out, buf, n - 1) == GY_NET_ERR_LEN);
    buf[2] = (uint8_t)(GY_NET_VERSION + 1);
    CHECK(gy_net_proto_decode(&out, buf, n) == GY_NET_ERR_VERSION);
    buf[2] = GY_NET_VERSION;
    buf[0] = 'X';
    CHECK(gy_net_proto_decode(&out, buf, n) == GY_NET_ERR_ARG);
    buf[0] = GY_NET_MAGIC0;
    in.type = 0;
    CHECK(gy_net_proto_encode(&in, buf, (int)sizeof(buf)) == GY_NET_ERR_TYPE);
    in.type = GY_NET_PING;
    in.len = GY_NET_MAX_PAYLOAD + 1;
    CHECK(gy_net_proto_encode(&in, buf, (int)sizeof(buf)) == GY_NET_ERR_LEN);
    return 0;
}

static int test_proto_typed(void)
{
    GyNetMsg m;
    uint8_t buf[GY_NET_MAX_PACKET];
    int n;

    {
        GyNetAdvRoom v = {123, 2, GY_NET_PHASE_PLAYING, 1, 2}, o;
        CHECK(gy_net_pack_adv_room(&m, &v) == GY_NET_OK);
        n = gy_net_proto_encode(&m, buf, (int)sizeof(buf));
        CHECK(n > 0 && gy_net_proto_decode(&m, buf, n) == GY_NET_OK);
        CHECK(gy_net_unpack_adv_room(&m, &o) == GY_NET_OK);
        CHECK(o.room_id == v.room_id && o.players == 2 && o.phase == v.phase &&
              o.spectators == 1 && o.max_spectators == 2);
    }
    {
        GyNetJoinReq v = {GY_NET_ROLE_PLAYER, 5}, o;
        GyNetJoinAck a = {GY_NET_COLOR_BLUE, 1, 99, 5, 8}, ao;
        GyNetJoinReject r = {GY_NET_ERR_FULL}, ro;
        CHECK(gy_net_pack_join_req(&m, &v) == GY_NET_OK);
        CHECK(gy_net_unpack_join_req(&m, &o) == GY_NET_OK && o.role == v.role && o.room_id == 5);
        CHECK(gy_net_pack_join_ack(&m, &a) == GY_NET_OK);
        CHECK(gy_net_unpack_join_ack(&m, &ao) == GY_NET_OK && ao.token == 99 && ao.seq == 8);
        CHECK(gy_net_pack_join_reject(&m, &r) == GY_NET_OK);
        CHECK(gy_net_unpack_join_reject(&m, &ro) == GY_NET_OK &&
              ro.reason == (uint8_t)GY_NET_ERR_FULL);
    }
    {
        GyNetRoomState v = {GY_NET_PHASE_STARTING, GY_NET_COLOR_RED, GY_NET_COLOR_BLUE,
                            1, 0, 2, 1, 5, 12}, o;
        CHECK(gy_net_pack_room_state(&m, &v) == GY_NET_OK);
        CHECK(gy_net_unpack_room_state(&m, &o) == GY_NET_OK);
        CHECK(o.phase == v.phase && o.host_ready == 1 && o.spectator_count == 1 &&
              o.room_id == 5 && o.seq == 12);
    }
    {
        GyNetReady v = {1}, o;
        GyNetSetSeat s = {GY_NET_COLOR_BLUE}, so;
        GyNetLeave l = {GY_NET_REASON_LEFT}, lo;
        CHECK(gy_net_pack_ready(&m, &v) == GY_NET_OK);
        CHECK(gy_net_unpack_ready(&m, &o) == GY_NET_OK && o.ready == 1);
        CHECK(gy_net_pack_set_seat(&m, &s) == GY_NET_OK);
        CHECK(gy_net_unpack_set_seat(&m, &so) == GY_NET_OK && so.color == GY_NET_COLOR_BLUE);
        CHECK(gy_net_pack_leave(&m, &l) == GY_NET_OK);
        CHECK(gy_net_unpack_leave(&m, &lo) == GY_NET_OK && lo.reason == GY_NET_REASON_LEFT);
    }
    {
        GyNetStart v = {GY_NET_COLOR_RED, GY_NET_COLOR_BLUE, 1234, 9}, o;
        GyNetMoveReq mv = {GY_NET_COLOR_RED, 0, GY_UP, 1, 0, 10}, mo;
        GyNetResult r = {GY_RED_WIN, GY_NET_REASON_NORMAL, 11}, ro;
        CHECK(gy_net_pack_start(&m, &v) == GY_NET_OK);
        CHECK(gy_net_unpack_start(&m, &o) == GY_NET_OK && o.seed == 1234 && o.seq == 9);
        CHECK(gy_net_pack_move_req(&m, &mv) == GY_NET_OK);
        CHECK(gy_net_unpack_move_req(&m, &mo) == GY_NET_OK && mo.direction == GY_UP && mo.seq == 10);
        CHECK(gy_net_pack_result(&m, &r) == GY_NET_OK);
        CHECK(gy_net_unpack_result(&m, &ro) == GY_NET_OK && ro.outcome == GY_RED_WIN);
    }
    {
        GyNetPreview v = {GY_NET_COLOR_RED, GY_UP, 2, 0, 0x1234, 13}, o;
        GyNetPreviewClear c = {GY_NET_COLOR_RED}, co;
        GyNetRejoin r = {77, 14}, ro;
        GyNetReject j = {GY_NET_ERR_TURN, 15}, jo;
        CHECK(gy_net_pack_preview(&m, &v) == GY_NET_OK);
        CHECK(gy_net_unpack_preview(&m, &o) == GY_NET_OK && o.indicator == 0x1234 && o.seq == 13);
        CHECK(gy_net_pack_preview_clear(&m, &c) == GY_NET_OK);
        CHECK(gy_net_unpack_preview_clear(&m, &co) == GY_NET_OK && co.side == GY_NET_COLOR_RED);
        CHECK(gy_net_pack_rejoin(&m, &r) == GY_NET_OK);
        CHECK(gy_net_unpack_rejoin(&m, &ro) == GY_NET_OK && ro.token == 77 && ro.last_seq == 14);
        CHECK(gy_net_pack_reject(&m, &j) == GY_NET_OK);
        CHECK(gy_net_unpack_reject(&m, &jo) == GY_NET_OK &&
              jo.reason == (uint8_t)GY_NET_ERR_TURN);
    }
    return 0;
}

static int test_snapshot_roundtrip(void)
{
    GyGame g;
    GyNetSnapshot s, o;
    GyNetMsg m;
    uint8_t buf[GY_NET_MAX_PACKET];
    int n;

    CHECK(gy_init(&g, gy_default_config(), 7));
    memset(&s, 0, sizeof(s));
    s.seq = 55;
    s.game = g;
    CHECK(gy_net_pack_snapshot(&m, &s) == GY_NET_OK);
    n = gy_net_proto_encode(&m, buf, (int)sizeof(buf));
    CHECK(n > 0);
    CHECK(gy_net_proto_decode(&m, buf, n) == GY_NET_OK);
    CHECK(gy_net_unpack_snapshot(&m, &o) == GY_NET_OK);
    CHECK(o.seq == 55 && o.game.config.size == g.config.size &&
          o.game.turn == g.turn && o.game.rng == g.rng);
    CHECK(memcmp(o.game.cells, g.cells, sizeof(g.cells)) == 0);
    return 0;
}

static int test_lobby(void)
{
    GyNetLobby lobby;
    GyNetAdvRoom a = {1, 1, GY_NET_PHASE_WAIT, 0, 2};
    GyNetAdvRoom b = {2, 2, GY_NET_PHASE_PLAYING, 1, 2};
    GyNetRoomSummary room;

    gy_net_lobby_init(&lobby);
    CHECK(gy_net_lobby_update(&lobby, &a, 0) == GY_NET_OK);
    CHECK(gy_net_lobby_update(&lobby, &b, 100) == GY_NET_OK);
    CHECK(lobby.count == 2);
    CHECK(gy_net_lobby_tick(&lobby, 2999) == 0);
    CHECK(lobby.count == 2);
    CHECK(gy_net_lobby_tick(&lobby, 3000) == 1);
    CHECK(lobby.count == 1);
    CHECK(lobby.rooms[0].room_id == 2);

    memset(&room, 0, sizeof(room));
    room.valid = 1;
    room.players = 1;
    CHECK(gy_net_lobby_click(&room, 2) == GY_NET_ROLE_PLAYER);
    room.players = 2;
    room.spectators = 1;
    CHECK(gy_net_lobby_click(&room, 2) == GY_NET_ROLE_SPECTATOR);
    room.spectators = 2;
    CHECK(gy_net_lobby_click(&room, 2) == -1);
    room.valid = 0;
    CHECK(gy_net_lobby_click(&room, 2) == -1);
    return 0;
}

static int start_game(GyNetRoom *host, uint32_t *player_token)
{
    uint8_t color = GY_NET_COLOR_NONE;
    CHECK(gy_net_room_join(host, GY_NET_ROLE_PLAYER, 0, player_token, &color) == GY_NET_OK);
    CHECK(color == GY_NET_COLOR_BLUE);
    CHECK(gy_net_room_set_ready(host, GY_NET_COLOR_RED, 1, 10) == GY_NET_OK);
    CHECK(gy_net_room_set_ready(host, GY_NET_COLOR_BLUE, 1, 20) == GY_NET_OK);
    CHECK(host->view.phase == GY_NET_PHASE_STARTING);
    CHECK(gy_net_room_tick(host, 3020) == 1);
    CHECK(host->view.phase == GY_NET_PHASE_PLAYING);
    CHECK(host->view.game_valid == 1);
    return 0;
}

static int test_room_join_ready_start(void)
{
    GyNetRoom host, client;
    GyNetSnapshot snap;
    uint32_t t1 = 0, t2 = 0;
    uint8_t c1 = GY_NET_COLOR_NONE, c2 = GY_NET_COLOR_NONE;

    gy_net_room_init_host(&host, 1001, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &t1, &c1) == GY_NET_OK);
    CHECK(c1 == GY_NET_COLOR_BLUE && t1 != 0);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &t2, &c2) == GY_NET_ERR_FULL);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_SPECTATOR, 0, &t2, &c2) == GY_NET_OK);
    CHECK(c2 == GY_NET_COLOR_NONE && t2 != 0);
    CHECK(host.view.spectator_count == 1);

    CHECK(gy_net_room_set_ready(&host, GY_NET_COLOR_RED, 1, 10) == GY_NET_OK);
    CHECK(host.view.phase == GY_NET_PHASE_WAIT);
    CHECK(gy_net_room_set_ready(&host, GY_NET_COLOR_BLUE, 1, 20) == GY_NET_OK);
    CHECK(host.view.phase == GY_NET_PHASE_STARTING);
    CHECK(gy_net_room_set_ready(&host, GY_NET_COLOR_BLUE, 0, 30) == GY_NET_OK);
    CHECK(host.view.phase == GY_NET_PHASE_WAIT);
    CHECK(gy_net_room_set_ready(&host, GY_NET_COLOR_BLUE, 1, 40) == GY_NET_OK);
    CHECK(gy_net_room_tick(&host, 3039) == 0);
    CHECK(gy_net_room_tick(&host, 3040) == 1);
    CHECK(host.view.phase == GY_NET_PHASE_PLAYING);

    gy_net_room_snapshot(&host, &snap);
    CHECK(snap.seq == host.view.seq);
    gy_net_room_init_client(&client, 1001, 0);
    CHECK(gy_net_room_apply_snapshot(&client, &snap) == 1);
    CHECK(client.view.seq == snap.seq && client.view.phase == GY_NET_PHASE_PLAYING);
    CHECK(gy_net_room_apply_snapshot(&client, &snap) == 0);
    {
        GyNetSnapshot same = snap;
        snap.seq++;
        CHECK(gy_net_room_apply_snapshot(&client, &snap) == 1);

        /* ROOM_STATE 与 SNAPSHOT 使用同一条 seq 时，快照不能被 room seq 丢掉。 */
        {
            GyNetRoomState rs;
            GyNetRoom c2;
            gy_net_room_state(&host, &rs);
            CHECK(rs.seq == host.view.seq);
            CHECK(same.seq == rs.seq);
            gy_net_room_init_client(&c2, 1001, 0);
            CHECK(gy_net_room_apply_room_state(&c2, &rs) == 1);
            CHECK(gy_net_room_apply_snapshot(&c2, &same) == 1);
            CHECK(c2.view.game_valid == 1);
            CHECK(c2.view.phase == GY_NET_PHASE_PLAYING);
        }
    }
    return 0;
}

static int test_room_swap_and_leave(void)
{
    GyNetRoom host;
    uint32_t t1 = 0;
    uint8_t c1 = GY_NET_COLOR_NONE;
    uint32_t host_token;

    gy_net_room_init_host(&host, 1002, 0, 2);
    host_token = host.seats[host.view.host_color].token;
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &t1, &c1) == GY_NET_OK);
    CHECK(gy_net_room_swap_seats(&host) == GY_NET_OK);
    CHECK(host.view.host_color == GY_NET_COLOR_BLUE);
    CHECK(host.view.guest_color == GY_NET_COLOR_RED);
    CHECK(host.seats[host.view.host_color].role == GY_NET_ROLE_HOST);
    CHECK(host.seats[host.view.host_color].token == host_token);
    CHECK(host.local_color == host.view.host_color);

    CHECK(gy_net_room_set_ready(&host, GY_NET_COLOR_RED, 1, 10) == GY_NET_OK);
    CHECK(gy_net_room_swap_seats(&host) == GY_NET_ERR_STATE);

    gy_net_room_init_host(&host, 1003, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &t1, &c1) == GY_NET_OK);
    CHECK(gy_net_room_leave(&host, host.view.host_color, 10) == GY_NET_LEAVE_PROMOTED);
    CHECK(host.view.host_color == GY_NET_COLOR_BLUE && host.view.player_count == 1);
    CHECK(host.view.phase == GY_NET_PHASE_WAIT);

    gy_net_room_init_host(&host, 1004, 0, 2);
    CHECK(gy_net_room_leave(&host, host.view.host_color, 10) == GY_NET_LEAVE_CLOSED);
    CHECK(host.view.phase == GY_NET_PHASE_CLOSED);
    return 0;
}

static int first_valid_direction(const GyGame *g)
{
    int d;
    for (d = 0; d < 4; ++d)
        if (gy_eligible_count(g, d) > 0) return d;
    return -1;
}

static int test_room_move_and_reject(void)
{
    GyNetRoom host, client;
    GyNetSnapshot snap;
    uint32_t token = 0;
    int d;

    gy_net_room_init_host(&host, 1005, 0, 2);
    CHECK(start_game(&host, &token) == 0);
    CHECK(gy_net_room_move(&host, GY_NET_COLOR_BLUE, GY_UP, 1, 4000) == GY_NET_ERR_TURN);
    d = first_valid_direction(&host.view.game);
    CHECK(d >= 0);
    CHECK(gy_net_room_move(&host, GY_NET_COLOR_RED, d, 1, 4100) == GY_NET_OK);
    CHECK(host.view.seq > 0);
    CHECK(host.view.game.turn == GY_NET_COLOR_BLUE);
    CHECK(gy_net_room_move(&host, GY_NET_COLOR_RED, d, 99, 4200) == GY_NET_ERR_TURN);

    gy_net_room_snapshot(&host, &snap);
    gy_net_room_init_client(&client, 1005, 0);
    CHECK(gy_net_room_apply_snapshot(&client, &snap) == 1);
    snap.seq--;
    CHECK(gy_net_room_apply_snapshot(&client, &snap) == 0);

    CHECK(gy_net_room_resign(&host, GY_NET_COLOR_BLUE, 4300) == GY_NET_OK);
    CHECK(host.view.phase == GY_NET_PHASE_RESULT);
    CHECK(host.view.result == GY_RED_WIN);

    /* 结算后保留约 10s，然后关闭。 */
    CHECK(gy_net_room_tick(&host, 14299) == 0);
    CHECK(host.view.phase == GY_NET_PHASE_RESULT);
    CHECK(gy_net_room_tick(&host, 14300) == 1);
    CHECK(host.view.phase == GY_NET_PHASE_CLOSED);
    return 0;
}

static int test_room_place(void)
{
    GyNetRoom host;
    uint32_t token = 0;

    gy_net_room_init_host(&host, 1005, 0, 2);
    CHECK(start_game(&host, &token) == 0);
    host.view.game.coins[GY_NET_COLOR_RED] = 10;
    CHECK(gy_net_room_place(&host, GY_NET_COLOR_BLUE, 0, 4000) == GY_NET_ERR_TURN);
    CHECK(gy_net_room_place(&host, GY_NET_COLOR_RED, -1, 4100) == GY_NET_ERR_ACTION);
    CHECK(gy_net_room_place(&host, GY_NET_COLOR_RED, 0, 4200) == GY_NET_OK);
    CHECK(host.view.game.turn == GY_NET_COLOR_BLUE);
    return 0;
}

static int test_room_state_sync(void)
{
    GyNetRoom host, client;
    GyNetRoomState state;
    uint32_t token = 0;
    uint8_t color = GY_NET_COLOR_NONE;

    gy_net_room_init_host(&host, 1011, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &token, &color) == GY_NET_OK);
    gy_net_room_state(&host, &state);
    gy_net_room_init_client(&client, 1011, 0);
    CHECK(gy_net_room_apply_room_state(&client, &state) == 1);
    CHECK(client.view.player_count == 2);
    CHECK(client.view.host_color == host.view.host_color);
    CHECK(client.view.guest_color == host.view.guest_color);
    CHECK(gy_net_room_apply_room_state(&client, &state) == 0);
    state.seq++;
    CHECK(gy_net_room_apply_room_state(&client, &state) == 1);
    return 0;
}

static int test_preview_privacy(void)
{
    GyNetRoom host;
    GyNetPreview p;
    uint32_t token = 0;
    uint8_t color = GY_NET_COLOR_NONE;

    gy_net_room_init_host(&host, 1006, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &token, &color) == GY_NET_OK);
    memset(&p, 0, sizeof(p));
    p.side = GY_NET_COLOR_RED;
    p.direction = GY_UP;
    p.count = 1;
    p.indicator = 0x1234;
    CHECK(gy_net_room_preview(&host, &p) == 1);
    CHECK(gy_net_room_preview(&host, &p) == 0);      /* 方向/指示点不变 -> 限流 */
    CHECK(gy_net_room_preview_visible(&host, GY_NET_ROLE_HOST, GY_NET_COLOR_RED, GY_NET_COLOR_RED) == 1);
    CHECK(gy_net_room_preview_visible(&host, GY_NET_ROLE_PLAYER, GY_NET_COLOR_BLUE, GY_NET_COLOR_RED) == 0);
    CHECK(gy_net_room_preview_visible(&host, GY_NET_ROLE_SPECTATOR, GY_NET_COLOR_NONE, GY_NET_COLOR_RED) == 1);

    p.side = GY_NET_COLOR_BLUE;
    p.indicator = 0x5678;
    CHECK(gy_net_room_preview(&host, &p) == 1);
    CHECK(gy_net_room_preview_visible(&host, GY_NET_ROLE_HOST, GY_NET_COLOR_RED, GY_NET_COLOR_BLUE) == 0);
    CHECK(gy_net_room_preview_visible(&host, GY_NET_ROLE_SPECTATOR, GY_NET_COLOR_NONE, GY_NET_COLOR_BLUE) == 1);
    CHECK(gy_net_room_preview_clear(&host, GY_NET_COLOR_RED) == 1);
    CHECK(gy_net_room_preview_visible(&host, GY_NET_ROLE_SPECTATOR, GY_NET_COLOR_NONE, GY_NET_COLOR_RED) == 0);
    return 0;
}

static int test_disconnect_rejoin(void)
{
    GyNetRoom host;
    uint32_t token = 0;
    uint8_t color = GY_NET_COLOR_NONE;

    gy_net_room_init_host(&host, 1007, 0, 2);
    CHECK(start_game(&host, &token) == 0);
    CHECK(gy_net_room_disconnect_token(&host, token, 5000) == GY_NET_OK);
    CHECK(gy_net_room_tick(&host, 14999) == 0);
    CHECK(host.view.phase == GY_NET_PHASE_PLAYING);
    CHECK(gy_net_room_rejoin(&host, token, 7, 10000, &color) == GY_NET_OK);
    CHECK(color == GY_NET_COLOR_BLUE);

    CHECK(gy_net_room_disconnect_token(&host, token, 20000) == GY_NET_OK);
    CHECK(gy_net_room_tick(&host, 29999) == 0);
    CHECK(gy_net_room_tick(&host, 30001) == 1);
    CHECK(host.view.phase == GY_NET_PHASE_RESULT);
    CHECK(host.view.result == GY_RED_WIN);
    CHECK(host.view.result_reason == GY_NET_REASON_DISCONNECT);
    CHECK(gy_net_room_rejoin(&host, token, 8, 30001, &color) == GY_NET_ERR_TOKEN);

    /* 等待阶段断线只释放席位，不判负。 */
    gy_net_room_init_host(&host, 1008, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &token, &color) == GY_NET_OK);
    CHECK(gy_net_room_disconnect_token(&host, token, 1000) == GY_NET_OK);
    CHECK(gy_net_room_tick(&host, 11001) == 1);
    CHECK(host.view.player_count == 1);
    CHECK(host.view.phase == GY_NET_PHASE_WAIT);
    CHECK(host.view.result == GY_PLAYING);

    /* 旁观者断线不冻结对局，超时移除。 */
    gy_net_room_init_host(&host, 1009, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_SPECTATOR, 0, &token, &color) == GY_NET_OK);
    CHECK(gy_net_room_disconnect_token(&host, token, 1000) == GY_NET_OK);
    CHECK(gy_net_room_tick(&host, 11001) == 1);
    CHECK(host.view.spectator_count == 0);
    return 0;
}

static int test_room_config_and_promote(void)
{
    GyNetRoom host, client;
    GyConfig cfg = gy_default_config();
    uint32_t token = 0;

    gy_net_room_init_host(&host, 1012, 0, 2);
    cfg.size = 7;
    CHECK(gy_net_room_set_config(&host, &cfg) == GY_NET_OK);
    cfg.size = 99;
    CHECK(gy_net_room_set_config(&host, &cfg) == GY_NET_ERR_RANGE);
    CHECK(start_game(&host, &token) == 0);
    CHECK(host.view.game.config.size == 7);

    gy_net_room_init_client(&client, 1013, 0);
    client.token = 0x1234;
    client.local_color = GY_NET_COLOR_BLUE;
    CHECK(gy_net_room_promote_client(&client, GY_NET_COLOR_BLUE, 100) == GY_NET_OK);
    CHECK(client.role == GY_NET_ROLE_HOST);
    CHECK(client.local_color == GY_NET_COLOR_BLUE);
    CHECK(client.view.player_count == 1);
    CHECK(client.seats[GY_NET_COLOR_BLUE].token == 0x1234);
    CHECK(client.seats[GY_NET_COLOR_BLUE].role == GY_NET_ROLE_HOST);

    gy_net_room_init_client(&client, 1013, 0);
    {
        GyNetResult r = {GY_RED_WIN, GY_NET_REASON_NORMAL, 5};
        CHECK(gy_net_room_apply_result(&client, &r) == 1);
        CHECK(client.view.phase == GY_NET_PHASE_RESULT);
        CHECK(client.view.result == GY_RED_WIN);
        CHECK(gy_net_room_apply_result(&client, &r) == 0);
    }
    return 0;
}

static int test_starting_disconnect_and_spectator_slot(void)
{
    GyNetRoom host;
    uint32_t t1 = 0, t2 = 0;
    uint8_t c1 = GY_NET_COLOR_NONE, c2 = GY_NET_COLOR_NONE;

    gy_net_room_init_host(&host, 1014, 0, 2);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0, &t1, &c1) == GY_NET_OK);
    CHECK(gy_net_room_set_ready(&host, GY_NET_COLOR_RED, 1, 10) == GY_NET_OK);
    CHECK(gy_net_room_set_ready(&host, GY_NET_COLOR_BLUE, 1, 20) == GY_NET_OK);
    CHECK(host.view.phase == GY_NET_PHASE_STARTING);
    CHECK(gy_net_room_disconnect_token(&host, t1, 1000) == GY_NET_OK);
    CHECK(gy_net_room_tick(&host, 3020) == 1);
    CHECK(host.view.phase == GY_NET_PHASE_WAIT);   /* 倒计时中断线不能开局 */
    CHECK(gy_net_room_tick(&host, 11001) == 1);
    CHECK(host.view.player_count == 1);            /* 等待期超时只释放席位 */

    /* 旁观者 10s 重连窗口内不能被新旁观者顶掉 token。 */
    gy_net_room_init_host(&host, 1015, 0, 1);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_SPECTATOR, 0, &t1, &c1) == GY_NET_OK);
    CHECK(gy_net_room_disconnect_token(&host, t1, 1000) == GY_NET_OK);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_SPECTATOR, 2000, &t2, &c2) == GY_NET_ERR_FULL);
    CHECK(gy_net_room_rejoin(&host, t1, 0, 2000, &c1) == GY_NET_OK);
    CHECK(c1 == GY_NET_COLOR_NONE);
    CHECK(gy_net_room_disconnect_token(&host, t1, 3000) == GY_NET_OK);
    CHECK(gy_net_room_tick(&host, 14000) == 1);
    CHECK(gy_net_room_join(&host, GY_NET_ROLE_SPECTATOR, 14001, &t2, &c2) == GY_NET_OK);
    return 0;
}

static int test_client_host_lost(void)
{
    GyNetRoom client;

    gy_net_room_init_client(&client, 1010, 0);
    gy_net_room_client_disconnect(&client, 0);
    CHECK(gy_net_room_client_host_lost(&client, 9999) == 0);
    CHECK(client.view.local_terminal == 0);
    CHECK(gy_net_room_client_host_lost(&client, 10000) == 1);
    CHECK(client.view.local_terminal == 1);
    CHECK(client.view.result == GY_PLAYING);   /* 不是权威 RESULT */
    gy_net_room_client_reconnect(&client, 10001);
    CHECK(client.view.local_terminal == 0);
    CHECK(client.connected == 1);
    return 0;
}

static int test_memory_link(void)
{
    GyNetMemoryLink link;
    GyNetMsg in, out;

    gy_net_memory_link_init(&link);
    gy_net_msg_init(&in, GY_NET_MOVE_REQ, 3);
    in.len = 1;
    in.payload[0] = 9;
    CHECK(gy_net_memory_link_send_client(&link, &in) == 1);
    CHECK(gy_net_memory_link_recv_host(&link, &out) == 1);
    CHECK(out.type == GY_NET_MOVE_REQ && out.seq == 3 && out.payload[0] == 9);
    CHECK(gy_net_memory_link_recv_host(&link, &out) == 0);
    gy_net_memory_link_connect(&link, 0);
    CHECK(gy_net_memory_link_send_host(&link, &in) == 0);
    return 0;
}

static int test_selftests(void)
{
    CHECK(gy_net_proto_selftest() == 0);
    CHECK(gy_net_link_selftest() == 0);
    CHECK(gy_net_room_selftest() == 0);
    return 0;
}

int main(void)
{
    if (test_proto_envelope()) return 1;
    if (test_proto_typed()) return 2;
    if (test_snapshot_roundtrip()) return 3;
    if (test_lobby()) return 4;
    if (test_room_join_ready_start()) return 5;
    if (test_room_swap_and_leave()) return 6;
    if (test_room_move_and_reject()) return 7;
    if (test_room_place()) return 8;
    if (test_room_state_sync()) return 9;
    if (test_preview_privacy()) return 10;
    if (test_disconnect_rejoin()) return 11;
    if (test_room_config_and_promote()) return 12;
    if (test_starting_disconnect_and_spectator_slot()) return 13;
    if (test_client_host_lost()) return 14;
    if (test_memory_link()) return 14;
    if (test_selftests()) return 15;
    printf("geyi-net-tests: %d checks PASS\n", checks);
    return 0;
}
