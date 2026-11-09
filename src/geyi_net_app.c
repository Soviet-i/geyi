/*
 * geyi_net_app.c —— 联机应用层（REQ-501~REQ-513）
 *
 * 负责把 net_room（纯逻辑）与 BLE 传输、UI 事件粘起来：
 *   - 房主：处理 JOIN/READY/MOVE/PREVIEW/REJOIN，广播 ROOM_STATE/SNAPSHOT/RESULT；
 *   - 客户端：发 JOIN_REQ/MOVE_REQ/PREVIEW，接受房主快照；
 *   - 旁观者：只收状态/快照/预览，不发操作。
 * 桌面构建（无 GEYI_WITH_RTTHREAD）只保留状态与 no-op 传输，便于编译 UI。
 */
#include "geyi_net_app.h"
#include "geyi_config.h"
#include <string.h>

#ifdef GEYI_WITH_RTTHREAD
#include "geyi_net_ble.h"
#include "rtthread.h"
#define APP_LOG(fmt, ...) rt_kprintf("[%u][geyi:net] " fmt "\n", (unsigned)rt_tick_get(), ##__VA_ARGS__)
#else
#define APP_LOG(fmt, ...) do { } while (0)
#endif
#ifndef GY_NET_BLE_BROADCAST_CONN
#define GY_NET_BLE_BROADCAST_CONN 0xff
#endif

#define APP_MAX_CONN 4
#define APP_ROOM_TTL_MS 1500u
/* 房主 1s 一包；两次丢包仍允许点击，交给连接/ACK 超时裁决。 */
#define APP_ROOM_FRESH_MS 2500u
#define APP_HEARTBEAT_MS 1000u
/* 首次加入要等服务发现 + CCCD 写入；5s 在板端太短。 */
#define APP_JOIN_TIMEOUT_MS 12000u
/* 扫描到房间后连不上（房主已关房/离开）不应让用户干等 12s。 */
#define APP_CONNECT_TIMEOUT_MS 4000u
/* CCCD/服务发现完成后再等 ACK；房间已关时不能拖满 12s。 */
#define APP_JOIN_ACK_TIMEOUT_MS 5000u
/* 房主退出时 ROOM_STATE 可能因立即断链丢失；等待期客方在 2.5s 后自行接管。 */
#define APP_HOST_TAKEOVER_MS 2500u

/* 加入失败原因定义在 geyi_net_app.h，供 UI 与测试共用。 */

typedef struct {
    uint8_t  used;
    uint8_t  conn;       /* 0xff = 已断开，保留 token 等重连 */
    uint8_t  role;
    uint8_t  color;
    uint32_t token;
    uint32_t last_rx;
    uint32_t last_intent_seq;
    uint8_t  intent_seen;
} ConnMap;

static struct {
    int      active;       /* 0 无，1 房主，2 客户端/旁观 */
    int      role;
    GyNetRoom room;
    uint8_t  local_color;
    uint32_t token;
    uint32_t room_id;
    uint32_t now;
    uint32_t last_adv;
    uint32_t last_hb;
    uint32_t last_ping;
    uint32_t last_rx;
    uint8_t  dropped;
    uint8_t  spectator_lost;
    uint32_t drop_at;
    uint32_t reconnect_at;
    uint8_t  rejoin_sent;
    ConnMap  conns[APP_MAX_CONN];
    GyNetAppRoom rooms[GY_NET_APP_MAX_ROOMS];
    int      room_count;
    int      pending_join;
    uint8_t  join_sent;
    uint32_t join_at;
    uint32_t join_sent_at;
    uint8_t  join_role;       /* 本地预期角色，仅用于 UI */
    uint8_t  join_req_role;   /* 发给房主的角色：0/1/2(auto) */
    uint8_t  join_addr[6];
    uint8_t  join_addr_type;
    uint8_t  join_rejected;
    uint8_t  join_reason;
    uint8_t  join_connected;
    uint8_t  link_down;      /* 客户端 BLE 链路是否断开；接管必须以此为前提 */
    uint32_t join_ready_at;
    uint16_t local_player_id;
    uint8_t  pending_move;
    uint8_t  local_ready;
    uint8_t  ready_desired;  /* 客方希望设置的 ready */
    uint8_t  ready_pending;  /* 已发 READY，等房主状态确认 */
    uint8_t  need_fresh_join;/* REJOIN 被拒后等 WAIT 再 auto JOIN */
    uint32_t ready_at;
    uint32_t starting_seq;   /* 客方最近一次 STARTING 状态 seq */
    uint8_t  reack_left;     /* 换座后 JOIN_ACK 重发次数，防止单包丢失 */
    uint32_t reack_at;
    int      dir;
    int      count;
    int      last_dir;
    int      last_count;
    uint8_t  last_preview_valid;
    int      placing;
    int      place_col;
    GyNetPreview preview[2];
    uint8_t  preview_valid[2];
} g;

static int color_valid(uint8_t c) { return c == GY_NET_COLOR_RED || c == GY_NET_COLOR_BLUE; }
static void join_fail(uint8_t reason);

static void send_msg(uint8_t conn, GyNetMsg *msg)
{
#ifdef GEYI_WITH_RTTHREAD
    gy_net_ble_send(conn, msg);
#else
    (void)conn; (void)msg;
#endif
}
static void send_broadcast(GyNetMsg *msg) { send_msg(GY_NET_BLE_BROADCAST_CONN, msg); }

static void send_spectators(GyNetMsg *msg)
{
    int i;
    for (i = 0; i < APP_MAX_CONN; ++i)
        if (g.conns[i].used && g.conns[i].conn != 0xff &&
            g.conns[i].role == GY_NET_ROLE_SPECTATOR)
            send_msg(g.conns[i].conn, msg);
}

static void broadcast_state(void)
{
    GyNetRoomState s;
    GyNetMsg m;
    gy_net_room_state(&g.room, &s);
    if (gy_net_pack_room_state(&m, &s) == GY_NET_OK) send_broadcast(&m);
}
static void broadcast_snapshot(void)
{
    GyNetSnapshot s;
    GyNetMsg m;
    gy_net_room_snapshot(&g.room, &s);
    if (gy_net_pack_snapshot(&m, &s) == GY_NET_OK) send_broadcast(&m);
}
static void broadcast_result(void)
{
    GyNetResult r;
    GyNetMsg m;
    memset(&r, 0, sizeof(r));
    r.outcome = g.room.view.result;
    r.reason = g.room.view.result_reason;
    r.seq = g.room.view.seq;
    if (gy_net_pack_result(&m, &r) == GY_NET_OK) send_broadcast(&m);
}
static void send_state_to(uint8_t conn)
{
    GyNetRoomState s;
    GyNetMsg m;
    gy_net_room_state(&g.room, &s);
    if (gy_net_pack_room_state(&m, &s) == GY_NET_OK) send_msg(conn, &m);
}
static void send_snapshot_to(uint8_t conn)
{
    GyNetSnapshot s;
    GyNetMsg m;
    gy_net_room_snapshot(&g.room, &s);
    if (gy_net_pack_snapshot(&m, &s) == GY_NET_OK) send_msg(conn, &m);
}
static void send_join_ack(uint8_t conn, uint8_t color, uint32_t token, uint16_t player_id)
{
    GyNetJoinAck a;
    GyNetMsg m;
    memset(&a, 0, sizeof(a));
    a.color = color;
    a.seat = color == GY_NET_COLOR_RED ? 0 : color == GY_NET_COLOR_BLUE ? 1 : 0;
    a.player_id = player_id;
    a.token = token;
    a.room_id = g.room_id;
    a.seq = g.room.view.seq;
    if (gy_net_pack_join_ack(&m, &a) == GY_NET_OK) send_msg(conn, &m);
}
static void send_join_reject(uint8_t conn, uint8_t reason)
{
    GyNetJoinReject j;
    GyNetMsg m;
    j.reason = reason;
    if (gy_net_pack_join_reject(&m, &j) == GY_NET_OK) send_msg(conn, &m);
}
static void send_reject(uint8_t conn, uint8_t reason)
{
    GyNetReject r;
    GyNetMsg m;
    r.reason = reason;
    r.seq = g.room.view.seq;
    if (gy_net_pack_reject(&m, &r) == GY_NET_OK) send_msg(conn, &m);
}
static void send_preview_clear_to_spectators(uint8_t side)
{
    GyNetPreviewClear p;
    GyNetMsg m;
    memset(&p, 0, sizeof(p));
    p.side = side;
    if (gy_net_pack_preview_clear(&m, &p) == GY_NET_OK) send_spectators(&m);
}

static uint8_t conn_color(const ConnMap *c)
{
    int i;
    if (!c) return GY_NET_COLOR_NONE;
    /* 必须用 token 匹配当前席位；不能退回 c->color，否则已退出/被顶掉
     * 的旧连接还能用旧颜色发 READY/MOVE，操作到新客人身上。 */
    for (i = 0; i < 2; ++i)
        if (g.room.seats[i].token && g.room.seats[i].token == c->token)
            return (uint8_t)i;
    return GY_NET_COLOR_NONE;
}
static uint8_t conn_role(const ConnMap *c)
{
    uint8_t color = conn_color(c);
    if (!color_valid(color)) return GY_NET_ROLE_SPECTATOR;
    return g.room.seats[color].role;
}

static ConnMap *conn_find(uint8_t conn)
{
    int i;
    for (i = 0; i < APP_MAX_CONN; ++i)
        if (g.conns[i].used && g.conns[i].conn == conn) return &g.conns[i];
    return NULL;
}
static ConnMap *conn_alloc(uint8_t conn, uint8_t role, uint8_t color, uint32_t token)
{
    int i, slot = -1, stale = -1;
    for (i = 0; i < APP_MAX_CONN; ++i) {
        if (!g.conns[i].used && slot < 0) slot = i;
        else if (g.conns[i].used && g.conns[i].conn == 0xff && stale < 0) stale = i;
    }
    if (slot < 0) slot = stale;
    if (slot < 0) return NULL;
    memset(&g.conns[slot], 0, sizeof(g.conns[slot]));
    g.conns[slot].used = 1;
    g.conns[slot].conn = conn;
    g.conns[slot].role = role;
    g.conns[slot].color = color;
    g.conns[slot].token = token;
    g.conns[slot].last_rx = g.now;
    return &g.conns[slot];
}
#ifdef GEYI_WITH_RTTHREAD
static int ble_has_conn(uint8_t conn)
{
    uint8_t i;
    for (i = 0; i < gy_net_ble_conn_count(); ++i)
        if (gy_net_ble_conn_at(i) == conn) return 1;
    return 0;
}
#endif

static int host_sync_conns(void)
{
    int changed = 0;
#ifdef GEYI_WITH_RTTHREAD
    int i;
    for (i = 0; i < APP_MAX_CONN; ++i) {
        if (!g.conns[i].used || g.conns[i].conn == 0xff) continue;
        if (!ble_has_conn(g.conns[i].conn) ||
            (uint32_t)(g.now - g.conns[i].last_rx) >= 10000u) {
            if (gy_net_room_disconnect_token(&g.room, g.conns[i].token, g.now) == GY_NET_OK)
                changed = 1;
            g.conns[i].conn = 0xff;
        }
    }
#endif
    return changed;
}

static void host_update_adv(void)
{
#ifdef GEYI_WITH_RTTHREAD
    int connectable = (g.room.view.player_count < 2 ||
                       g.room.view.spectator_count < g.room.view.max_spectators) ? 1 : 0;
    gy_net_ble_host_update(g.room.view.player_count, g.room.view.phase,
                           g.room.view.spectator_count, g.room.view.max_spectators,
                           connectable);
#endif
}

static void host_phase_change(int old_phase)
{
    if (old_phase == GY_NET_PHASE_STARTING && g.room.view.phase == GY_NET_PHASE_PLAYING) {
        GyNetStart st;
        GyNetMsg m;
        st.host_color = g.room.view.host_color;
        st.guest_color = g.room.view.guest_color;
        st.seed = g.room_id;
        st.seq = g.room.view.seq;
        if (gy_net_pack_start(&m, &st) == GY_NET_OK) send_broadcast(&m);
        broadcast_state();
        broadcast_snapshot();
    } else if (old_phase != GY_NET_PHASE_RESULT &&
               g.room.view.phase == GY_NET_PHASE_RESULT) {
        broadcast_result();
        broadcast_state();
    }
}

static void host_process(uint8_t conn, const GyNetMsg *msg)
{
    ConnMap *rxconn = conn_find(conn);
    if (rxconn) rxconn->last_rx = g.now;
    switch (msg->type) {
    case GY_NET_JOIN_REQ: {
        GyNetJoinReq j;
        uint32_t token = 0;
        uint8_t color = GY_NET_COLOR_NONE;
        uint8_t room_role;
        if (gy_net_unpack_join_req(msg, &j) != GY_NET_OK) return;
        if (j.room_id != g.room.view.room_id) { send_join_reject(conn, GY_NET_ERR_STATE); return; }
        {
            ConnMap *existing = conn_find(conn);
            if (existing) {
                uint8_t ec = conn_color(existing);
                /* 重复 JOIN（ACK 丢失）才直接回 ACK；若旧 player token 已
                 * 不在席位上（被释放/被顶掉），释放旧映射后重新加入。 */
                if (ec != GY_NET_COLOR_NONE ||
                    existing->role == GY_NET_ROLE_SPECTATOR) {
                    send_join_ack(conn, ec, existing->token,
                                  gy_net_room_seat_id(&g.room, ec));
                    send_state_to(conn);
                    if (g.room.view.game_valid) send_snapshot_to(conn);
                    return;
                }
                existing->used = 0;
            }
        }
        if (j.role == 0) room_role = GY_NET_ROLE_PLAYER;
        else if (j.role == 1) room_role = GY_NET_ROLE_SPECTATOR;
        else {
            /* auto：大厅摘要可能滞后（刚有人退出仍显示 2/2），以房主
             * 当前真实空位为准，避免“1/2 却被塞成旁观”。 */
            room_role = (g.room.view.phase == GY_NET_PHASE_WAIT &&
                         g.room.view.player_count < 2)
                        ? GY_NET_ROLE_PLAYER : GY_NET_ROLE_SPECTATOR;
        }
        {
            int rc = gy_net_room_join(&g.room, room_role, j.player_id, g.now,
                                      &token, &color);
            /* 大厅摘要可能滞后：客人按“还有空位”发 PLAYER，但到房主这里
             * 已经满员。此时降级为旁观，而不是把客人踢回大厅。 */
            if (rc == GY_NET_ERR_FULL && room_role == GY_NET_ROLE_PLAYER) {
                room_role = GY_NET_ROLE_SPECTATOR;
                rc = gy_net_room_join(&g.room, room_role, j.player_id, g.now,
                                      &token, &color);
            }
            if (rc != GY_NET_OK) {
                send_join_reject(conn, (uint8_t)-rc);
                return;
            }
        }
        if (!conn_alloc(conn, room_role, color, token)) {
            gy_net_room_leave_token(&g.room, token, g.now);
            send_join_reject(conn, (uint8_t)(-GY_NET_ERR_FULL));
            broadcast_state();
            host_update_adv();
            return;
        }
        APP_LOG("join accepted color=%d", color);
        send_join_ack(conn, color, token, gy_net_room_seat_id(&g.room, color));
        send_state_to(conn);
        if (g.room.view.game_valid) send_snapshot_to(conn);
        broadcast_state();
        host_update_adv();
        break;
    }
    case GY_NET_READY: {
        GyNetReady r;
        ConnMap *c = conn_find(conn);
        if (!c || gy_net_unpack_ready(msg, &r) != GY_NET_OK) return;
        if (gy_net_room_set_ready(&g.room, conn_color(c), r.ready, g.now) == GY_NET_OK) {
            broadcast_state();
            host_update_adv();
        }
        break;
    }
    case GY_NET_SET_SEAT: {
        ConnMap *c = conn_find(conn);
        if (!c || conn_role(c) != GY_NET_ROLE_HOST) return;
        if (gy_net_room_swap_seats(&g.room) == GY_NET_OK) {
            int i;
            for (i = 0; i < APP_MAX_CONN; ++i)
                if (g.conns[i].used && g.conns[i].conn != 0xff &&
                    color_valid(conn_color(&g.conns[i])))
                    send_join_ack(g.conns[i].conn, conn_color(&g.conns[i]),
                                  g.conns[i].token,
                                  gy_net_room_seat_id(&g.room, conn_color(&g.conns[i])));
            broadcast_state();
        }
        break;
    }
    case GY_NET_LEAVE: {
        GyNetLeave l;
        ConnMap *c = conn_find(conn);
        int old_phase;
        if (!c || gy_net_unpack_leave(msg, &l) != GY_NET_OK) return;
        old_phase = g.room.view.phase;
        {
            uint8_t color = conn_color(c);
            int resign_only = (l.reason == GY_NET_REASON_RESIGN);
            if (g.room.view.phase == GY_NET_PHASE_PLAYING && color_valid(color)) {
                gy_net_room_resign(&g.room, color, g.now);
                if (!resign_only) {
                    /* 真正离房：释放席位并断开映射。 */
                    gy_net_room_leave_token(&g.room, c->token, g.now);
                    c->used = 0;
                }
                /* 仅认输：连接和席位保留，结算后还要回同一房间准备。 */
            } else {
                gy_net_room_leave_token(&g.room, c->token, g.now);
                c->used = 0;
            }
        }
        broadcast_state();
        host_phase_change(old_phase);
        host_update_adv();
        break;
    }
    case GY_NET_MOVE_REQ: {
        GyNetMoveReq mr;
        ConnMap *c = conn_find(conn);
        int rc;
        if (!c || gy_net_unpack_move_req(msg, &mr) != GY_NET_OK) return;
        if (c->intent_seen && mr.seq <= c->last_intent_seq) {
            send_reject(conn, (uint8_t)(-GY_NET_ERR_STATE));
            return;
        }
        if (mr.kind == 0)
            rc = gy_net_room_move(&g.room, conn_color(c), mr.direction, mr.count, g.now);
        else
            rc = gy_net_room_place(&g.room, conn_color(c), mr.column, g.now);
        if (rc != GY_NET_OK) {
            send_reject(conn, (uint8_t)(rc < 0 ? -rc : rc));
        } else {
            c->intent_seen = 1;
            c->last_intent_seq = mr.seq;
            gy_net_room_preview_clear(&g.room, conn_color(c));
            send_preview_clear_to_spectators(conn_color(c));
            broadcast_snapshot();
            host_phase_change(GY_NET_PHASE_PLAYING);
        }
        break;
    }
    case GY_NET_PREVIEW: {
        GyNetPreview p;
        ConnMap *c = conn_find(conn);
        GyNetMsg out;
        if (!c || gy_net_unpack_preview(msg, &p) != GY_NET_OK) return;
        p.side = conn_color(c);
        if (gy_net_room_preview(&g.room, &p) > 0) {
            if (gy_net_pack_preview(&out, &p) == GY_NET_OK) send_spectators(&out);
        }
        break;
    }
    case GY_NET_PREVIEW_CLEAR: {
        GyNetPreviewClear p;
        ConnMap *c = conn_find(conn);
        GyNetMsg out;
        if (!c || gy_net_unpack_preview_clear(msg, &p) != GY_NET_OK) return;
        p.side = conn_color(c);
        gy_net_room_preview_clear(&g.room, conn_color(c));
        if (gy_net_pack_preview_clear(&out, &p) == GY_NET_OK) send_spectators(&out);
        break;
    }
    case GY_NET_REJOIN: {
        GyNetRejoin r;
        uint8_t color = GY_NET_COLOR_NONE;
        ConnMap *c;
        if (gy_net_unpack_rejoin(msg, &r) != GY_NET_OK) return;
        if (gy_net_room_rejoin(&g.room, r.token, r.last_seq, g.now, &color) != GY_NET_OK) {
            send_reject(conn, (uint8_t)(-GY_NET_ERR_TOKEN));
            return;
        }
        c = conn_find(conn);
        if (!c) c = conn_alloc(conn, color == GY_NET_COLOR_NONE ? GY_NET_ROLE_SPECTATOR : GY_NET_ROLE_PLAYER, color, r.token);
        else { c->token = r.token; c->color = color; c->conn = conn; c->last_rx = g.now; }
        if (!c) {
            gy_net_room_disconnect_token(&g.room, r.token, g.now);
            send_join_reject(conn, (uint8_t)(-GY_NET_ERR_FULL));
            broadcast_state();
            host_update_adv();
            return;
        }
        send_join_ack(conn, color, r.token, gy_net_room_seat_id(&g.room, color));
        send_state_to(conn);
        send_snapshot_to(conn);
        /* 重连成功后其它端/大厅也要看到席位恢复。 */
        broadcast_state();
        host_update_adv();
        break;
    }
    case GY_NET_PING: {
        ConnMap *c = conn_find(conn);
        if (c) c->last_rx = g.now;
        break;
    }
    case GY_NET_HEARTBEAT:
        break;
    default:
        break;
    }
}

/* 等待期房主退出：本机从客方切换为房主并继续广播。
 * 状态消息正常时由 ROOM_STATE 触发；消息丢失时由断线超时兜底。 */
static void promote_self_to_host(uint8_t new_host_color, uint32_t now_ms)
{
    uint32_t token = g.token;
    gy_net_room_promote_client(&g.room, new_host_color, now_ms);
    if (!g.local_player_id)
        g.local_player_id = (uint16_t)gy_net_app_local_id();
    gy_net_room_set_player_id(&g.room, new_host_color, g.local_player_id);
    {
        GyConfig cfg;
        /* 接管后本机成为房主，新开一局使用本机联机参数。 */
        if (gy_param_apply(GY_MODE_PVP, &cfg))
            gy_net_room_set_config(&g.room, &cfg);
    }
    g.room.seats[new_host_color].token = token;
    g.token = token;
    g.active = 1;
    g.role = GY_NET_ROLE_HOST;
    g.local_color = new_host_color;
    g.local_ready = 0;
    g.ready_pending = 0;
    g.ready_desired = 0;
    g.need_fresh_join = 0;
    g.pending_join = 0;
    g.join_sent = 0;
    g.join_connected = 0;
    g.join_ready_at = 0;
    g.pending_move = 0;
    g.dropped = 0;
    g.drop_at = 0;
    g.preview_valid[0] = g.preview_valid[1] = 0;
    g.last_preview_valid = 0;
#ifdef GEYI_WITH_RTTHREAD
    gy_net_ble_scan_stop();
    if (gy_net_ble_client_conn() != 0xff)
        gy_net_ble_disconnect(gy_net_ble_client_conn());
    gy_net_ble_host_start(g.room_id, 1, GY_NET_PHASE_WAIT, 0, 2);
#endif
    APP_LOG("promote self to host color=%d id=%04X", new_host_color,
            (unsigned)g.local_player_id);
}

static void start_fresh_join(uint32_t now_ms)
{
    g.token = 0;
    g.local_color = GY_NET_COLOR_NONE;
    g.pending_join = 1;
    g.join_sent = 0;
    g.join_connected = g.link_down ? 0 : 1;
    g.join_ready_at = 0;
    g.join_at = now_ms;
    g.join_req_role = 2;
    g.dropped = 0;
    g.drop_at = 0;
    g.need_fresh_join = 0;
    g.ready_pending = 0;
    APP_LOG("fresh auto join");
}

static void client_process(const GyNetMsg *msg)
{
    g.last_rx = g.now;
    switch (msg->type) {
    case GY_NET_JOIN_ACK: {
        GyNetJoinAck a;
        if (gy_net_unpack_join_ack(msg, &a) != GY_NET_OK) return;
        if (a.room_id != g.room_id) return;
        if (!g.pending_join && g.token && a.token != g.token) return;
        if (a.color != GY_NET_COLOR_NONE &&
            a.color != GY_NET_COLOR_RED && a.color != GY_NET_COLOR_BLUE) return;
        {
            int same = (g.token == a.token && g.local_color == a.color &&
                        !g.pending_join);
            if (!same) g.local_ready = 0;   /* 换座/首次 ACK 才清本地 ready */
        }
        APP_LOG("join ack color=%d", a.color);
        g.token = a.token;
        g.local_color = a.color;
        g.local_player_id = a.player_id ? a.player_id : g.local_player_id;
        g.role = a.color == GY_NET_COLOR_NONE ? GY_NET_ROLE_SPECTATOR : GY_NET_ROLE_PLAYER;
        g.room.role = g.role;
        g.room.local_color = a.color;
        g.room.token = a.token;
        g.join_rejected = 0;
        g.join_reason = GY_NET_JOIN_REASON_NONE;
        g.pending_join = 0;
        g.join_sent = 0;
        g.join_connected = 0;
        g.join_ready_at = 0;
        g.ready_pending = 0;
        g.ready_desired = 0;
        g.need_fresh_join = 0;
        g.starting_seq = 0;
        g.dropped = 0;
        g.rejoin_sent = 0;
        g.drop_at = 0;
        gy_net_room_client_reconnect(&g.room, g.now);
        g.last_rx = g.now;
        break;
    }
    case GY_NET_JOIN_REJECT: {
        GyNetJoinReject j;
        if (gy_net_unpack_join_reject(msg, &j) != GY_NET_OK) return;
        join_fail(j.reason == (uint8_t)(-GY_NET_ERR_FULL) ||
                  j.reason == (uint8_t)GY_NET_ERR_FULL
                  ? GY_NET_JOIN_REASON_FULL : GY_NET_JOIN_REASON_NOT_FOUND);
        break;
    }
    case GY_NET_ROOM_STATE: {
        GyNetRoomState s;
        if (gy_net_unpack_room_state(msg, &s) != GY_NET_OK) return;
        if (s.room_id != g.room_id) return;
        if (s.phase == GY_NET_PHASE_CLOSED) {
            /* 单房主关房：其他板不能接管，提示房间不存在。 */
            join_fail(GY_NET_JOIN_REASON_NOT_FOUND);
#ifdef GEYI_WITH_RTTHREAD
            if (gy_net_ble_client_conn() != 0xff)
                gy_net_ble_disconnect(gy_net_ble_client_conn());
#endif
            return;
        }
        if (g.role == GY_NET_ROLE_PLAYER && g.local_color == s.host_color &&
            s.player_count == 1 && s.phase == GY_NET_PHASE_WAIT) {
            /* 两人房间的房主退出：房主转给本机，房间继续存在。 */
            promote_self_to_host(s.host_color, g.now);
            return;
        }
        {
            uint8_t old_phase = g.room.view.phase;
            int applied = gy_net_room_apply_room_state(&g.room, &s);
            if (applied && s.phase == GY_NET_PHASE_STARTING &&
                (old_phase != GY_NET_PHASE_STARTING ||
                 s.seq != g.starting_seq)) {
                /* ROOM_STATE 不带时间戳；客户端从收到 STARTING 的时刻
                 * 本地起算 3/2/1，否则会用 0 起算一直显示 1s。 */
                g.room.view.phase_since_ms = g.now;
                g.starting_seq = s.seq;
            }
            if (s.phase != GY_NET_PHASE_STARTING) g.starting_seq = 0;
        }
        if (color_valid(g.local_color)) {
            uint8_t auth = (g.local_color == s.host_color) ? s.host_ready : s.guest_ready;
            if (g.ready_pending) {
                if (auth == g.ready_desired) {
                    g.ready_pending = 0;
                    g.local_ready = auth;
                }
                /* 房主还没收到/没应用：保留乐观值，tick 继续重发。 */
            } else {
                g.local_ready = auth;
            }
        }
        if (g.local_color == GY_NET_COLOR_NONE && s.player_count >= 2 && g.role != GY_NET_ROLE_SPECTATOR)
            g.role = GY_NET_ROLE_SPECTATOR;
        break;
    }
    case GY_NET_SNAPSHOT: {
        GyNetSnapshot s;
        if (gy_net_unpack_snapshot(msg, &s) != GY_NET_OK) return;
        gy_net_room_apply_snapshot(&g.room, &s);
        g.pending_move = 0;
        g.placing = 0;
        g.last_preview_valid = 0;
        if (color_valid(g.local_color)) g.preview_valid[g.local_color] = 0;
        break;
    }
    case GY_NET_RESULT: {
        GyNetResult r;
        if (gy_net_unpack_result(msg, &r) != GY_NET_OK) return;
        gy_net_room_apply_result(&g.room, &r);
        break;
    }
    case GY_NET_REJECT: {
        GyNetReject r;
        if (gy_net_unpack_reject(msg, &r) != GY_NET_OK) return;
        g.pending_move = 0;
        g.last_preview_valid = 0;
        if (color_valid(g.local_color)) g.preview_valid[g.local_color] = 0;
        if (r.reason == (uint8_t)(-GY_NET_ERR_TOKEN) ||
            r.reason == (uint8_t)GY_NET_ERR_TOKEN) {
            /* WAIT 已立即释放旧席位，REJOIN 必然失败；链路还在时用
             * auto 重新 JOIN，绝不能靠接管（否则会与仍活着的房主双主）。 */
            g.rejoin_sent = 0;
            g.need_fresh_join = 1;
            if (g.role == GY_NET_ROLE_PLAYER &&
                g.room.view.phase == GY_NET_PHASE_WAIT) {
                start_fresh_join(g.now);
            }
        }
        break;
    }
    case GY_NET_PREVIEW: {
        GyNetPreview p;
        if (gy_net_unpack_preview(msg, &p) != GY_NET_OK) return;
        if (g.role == GY_NET_ROLE_SPECTATOR && color_valid(p.side)) {
            g.preview[p.side] = p;
            g.preview_valid[p.side] = 1;
        }
        break;
    }
    case GY_NET_PREVIEW_CLEAR: {
        GyNetPreviewClear p;
        if (gy_net_unpack_preview_clear(msg, &p) != GY_NET_OK) return;
        if (color_valid(p.side)) g.preview_valid[p.side] = 0;
        break;
    }
    case GY_NET_HEARTBEAT:
        g.last_rx = g.now;
        break;
    default:
        break;
    }
}

static void lobby_remove_room(uint32_t room_id)
{
    int i, w = 0;
    for (i = 0; i < g.room_count; ++i) {
        if (g.rooms[i].room_id == room_id) continue;
        if (w != i) g.rooms[w] = g.rooms[i];
        w++;
    }
    g.room_count = w;
}

static void join_fail(uint8_t reason)
{
    g.join_rejected = 1;
    g.join_reason = reason;
    g.pending_join = 0;
    g.join_sent = 0;
    g.join_connected = 0;
    g.join_ready_at = 0;
    g.active = 0;
    g.role = GY_NET_ROLE_SPECTATOR;
    g.local_color = GY_NET_COLOR_NONE;
    lobby_remove_room(g.room_id);
    memset(&g.room, 0, sizeof(g.room));
#ifdef GEYI_WITH_RTTHREAD
    if (gy_net_ble_client_conn() != 0xff)
        gy_net_ble_disconnect(gy_net_ble_client_conn());
#endif
}

static void lobby_drain(void)
{
#ifdef GEYI_WITH_RTTHREAD
    GyNetBleAdv adv;
    int i, slot;
    while (gy_net_ble_adv_recv(&adv)) {
        if (adv.adv.phase == GY_NET_PHASE_CLOSED || adv.adv.players == 0) {
            /* 房主关房广播：立即移除，不等 TTL。 */
            lobby_remove_room(adv.adv.room_id);
            continue;
        }
        slot = -1;
        for (i = 0; i < g.room_count; ++i)
            if (g.rooms[i].room_id == adv.adv.room_id) { slot = i; break; }
        if (slot < 0) {
            if (g.room_count >= GY_NET_APP_MAX_ROOMS) continue;
            slot = g.room_count++;
        }
        g.rooms[slot].room_id = adv.adv.room_id;
        g.rooms[slot].players = adv.adv.players;
        g.rooms[slot].phase = adv.adv.phase;
        g.rooms[slot].spectators = adv.adv.spectators;
        g.rooms[slot].max_spectators = adv.adv.max_spectators;
        memcpy(g.rooms[slot].addr, adv.addr, 6);
        g.rooms[slot].addr_type = adv.addr_type;
        g.rooms[slot].rssi = adv.rssi;
        g.rooms[slot].seen_ms = g.now;
    }
#endif
    {
        int i, w = 0;
        for (i = 0; i < g.room_count; ++i)
            if ((uint32_t)(g.now - g.rooms[i].seen_ms) < APP_ROOM_TTL_MS)
                g.rooms[w++] = g.rooms[i];
        g.room_count = w;
    }
}

void gy_net_app_init(void)
{
    memset(&g, 0, sizeof(g));
    g.role = GY_NET_ROLE_SPECTATOR;
    g.local_color = GY_NET_COLOR_NONE;
    g.link_down = 1;
#ifdef GEYI_WITH_RTTHREAD
    gy_net_ble_init();
#endif
}

void gy_net_app_tick(uint32_t now_ms)
{
    g.now = now_ms;
#ifdef GEYI_WITH_RTTHREAD
    gy_net_ble_poll();
#endif
    lobby_drain();
#ifdef GEYI_WITH_RTTHREAD
    {
        GyNetBleRx rx;
        while (gy_net_ble_recv(&rx)) {
            if (g.active == 1) host_process(rx.conn, &rx.msg);
            else if (g.active == 2 &&
                     rx.conn == gy_net_ble_client_conn()) {
                client_process(&rx.msg);
            }
        }
    }
#endif
    if (g.active == 1) {
        int old_phase;
        int changed = 0;
        if (!g.local_player_id) {
            g.local_player_id = (uint16_t)gy_net_app_local_id();
            if (g.local_player_id) {
                gy_net_room_set_player_id(&g.room, g.local_color, g.local_player_id);
                changed = 1;
            }
        }
        changed |= host_sync_conns();
        old_phase = g.room.view.phase;
        changed |= gy_net_room_tick(&g.room, now_ms);
        host_phase_change(old_phase);
        if (changed) {
            /* 席位/准备/阶段变化必须立即同步给客户端和大厅广播，
             * 否则房客退出后大厅会继续显示 2/2。 */
            broadcast_state();
            host_update_adv();
        }
        if (g.room.view.phase == GY_NET_PHASE_CLOSED) {
            gy_net_app_leave();
            return;
        }
        if ((uint32_t)(now_ms - g.last_hb) >= APP_HEARTBEAT_MS) {
            GyNetMsg m;
            g.last_hb = now_ms;
            gy_net_msg_init(&m, GY_NET_HEARTBEAT, g.room.view.seq);
            send_broadcast(&m);
            /* 周期性重发权威状态：READY/STARTING/RESULT→WAIT 任一包丢失
             * 都能在 1s 内恢复，而不是永久失步。 */
            broadcast_state();
        }
        if ((uint32_t)(now_ms - g.last_adv) >= APP_HEARTBEAT_MS) {
            g.last_adv = now_ms;
            host_update_adv();
        }
        if (g.reack_left && (uint32_t)(now_ms - g.reack_at) >= 1000u) {
            int i;
            g.reack_at = now_ms;
            g.reack_left--;
            for (i = 0; i < APP_MAX_CONN; ++i) {
                uint8_t color;
                if (!g.conns[i].used || g.conns[i].conn == 0xff) continue;
                color = conn_color(&g.conns[i]);
                if (color_valid(color))
                    send_join_ack(g.conns[i].conn, color, g.conns[i].token,
                                  gy_net_room_seat_id(&g.room, color));
            }
        }
    } else if (g.active == 2) {
#ifdef GEYI_WITH_RTTHREAD
        uint8_t conn = gy_net_ble_client_conn();
        g.link_down = (conn == 0xff) ? 1 : 0;
        if (g.need_fresh_join && g.room.view.phase == GY_NET_PHASE_WAIT)
            start_fresh_join(now_ms);
        if (g.pending_join && conn == 0xff &&
            (uint32_t)(now_ms - g.reconnect_at) >= 1000u) {
            g.reconnect_at = now_ms;
            gy_net_ble_connect(g.join_addr, g.join_addr_type);
        }
        if (g.pending_join && conn != 0xff) {
            g.join_connected = 1;
        } else if (g.pending_join && conn == 0xff && g.join_connected) {
            /* 链路瞬断/房主已关：重新给 4s 连接窗口，避免用最初 join_at
             * 立即误判；4s 内仍连不上才提示房间不存在。 */
            g.join_connected = 0;
            g.join_ready_at = 0;
            g.join_at = now_ms;
        }
        if (g.pending_join && !g.join_sent && gy_net_ble_write_ready()) {
            GyNetJoinReq j;
            GyNetMsg m;
            j.role = g.join_req_role;
            j.player_id = g.local_player_id ? g.local_player_id : (uint16_t)gy_net_app_local_id();
            j.room_id = g.room_id;
            if (gy_net_pack_join_req(&m, &j) == GY_NET_OK) {
                send_msg(conn, &m);
                g.join_sent = 1;
                g.join_sent_at = now_ms;
                if (!g.join_ready_at) {
                    g.join_ready_at = now_ms;
                    APP_LOG("join req sent");
                }
                g.last_rx = now_ms;
            }
        }
        /* JOIN_ACK 可能因 CCCD 尚未生效而丢失；400ms 未收到 ACK 就重发。 */
        if (g.pending_join && g.join_sent &&
            (uint32_t)(now_ms - g.join_sent_at) >= 400u) {
            g.join_sent = 0;
        }
        if (g.pending_join && g.join_ready_at &&
            (uint32_t)(now_ms - g.join_ready_at) >= APP_JOIN_ACK_TIMEOUT_MS) {
            join_fail(GY_NET_JOIN_REASON_NOT_FOUND);
            return;
        }
        if (!g.pending_join) g.join_ready_at = 0;
        /* 连不上通常意味着房主已关房（大厅摘要还没过期）。 */
        if (g.pending_join && !g.join_connected &&
            (uint32_t)(now_ms - g.join_at) >= APP_CONNECT_TIMEOUT_MS) {
            join_fail(GY_NET_JOIN_REASON_NOT_FOUND);
            return;
        }
        if (g.pending_join && g.join_connected &&
            (uint32_t)(now_ms - g.join_at) >= APP_JOIN_TIMEOUT_MS) {
            join_fail(GY_NET_JOIN_REASON_TIMEOUT);
            return;
        }
        if (conn == 0xff && !g.dropped && !g.pending_join) {
            g.dropped = 1;
            g.drop_at = now_ms;
            gy_net_room_client_disconnect(&g.room, now_ms);
        }
        if (!g.pending_join && !g.dropped &&
            (uint32_t)(now_ms - g.last_rx) >= 10000u) {
            g.dropped = 1;
            g.drop_at = now_ms;
            gy_net_room_client_disconnect(&g.room, now_ms);
        }
        if (g.dropped && !g.room.view.local_terminal &&
            (uint32_t)(now_ms - g.drop_at) < GY_NET_RECONNECT_MS) {
            if (conn == 0xff && (uint32_t)(now_ms - g.reconnect_at) >= 1000u) {
                g.reconnect_at = now_ms;
                gy_net_ble_connect(g.join_addr, g.join_addr_type);
            }
            if (conn != 0xff && gy_net_ble_ready() && !g.rejoin_sent && g.token) {
                GyNetRejoin r;
                GyNetMsg m;
                r.token = g.token;
                r.last_seq = g.room.view.seq;
                if (gy_net_pack_rejoin(&m, &r) == GY_NET_OK) {
                    send_msg(conn, &m);
                    g.rejoin_sent = 1;
                }
            }
        }
        if ((uint32_t)(now_ms - g.last_ping) >= APP_HEARTBEAT_MS && conn != 0xff) {
            GyNetMsg m;
            g.last_ping = now_ms;
            gy_net_msg_init(&m, GY_NET_PING, g.room.view.seq);
            send_msg(conn, &m);
        }
#endif
        if (g.dropped && g.link_down && !g.room.view.local_terminal &&
            g.role == GY_NET_ROLE_PLAYER && color_valid(g.local_color) &&
            g.room.view.phase == GY_NET_PHASE_WAIT &&
            g.room.view.player_count >= 2 &&
            (uint32_t)(now_ms - g.drop_at) >= APP_HOST_TAKEOVER_MS) {
            /* 房主退出时通知可能没发出来；等待期两人房里客方接管，
             * 避免房间永远卡在“客人 + 房主已断”。 */
            promote_self_to_host(g.local_color, now_ms);
            return;
        }
        if (g.ready_pending) {
            if (g.room.view.phase != GY_NET_PHASE_WAIT &&
                g.room.view.phase != GY_NET_PHASE_STARTING) {
                g.ready_pending = 0;
            } else if ((uint32_t)(now_ms - g.ready_at) >= 500u) {
                /* READY 丢包/房主未处理：500ms 重发，直到 ROOM_STATE 确认。 */
                gy_net_app_set_ready(g.ready_desired);
            }
        }
        if (gy_net_room_client_host_lost(&g.room, now_ms) &&
            g.role == GY_NET_ROLE_SPECTATOR) {
            g.spectator_lost = 1;
            g.active = 0;
        }
    }
}

int gy_net_app_active(void) { return g.active; }
int gy_net_app_role(void) { return g.role; }
int gy_net_app_phase(void) { return g.room.view.phase; }
int gy_net_app_local_color(void) { return g.local_color; }
int gy_net_app_host_color(void) { return g.room.view.host_color; }
int gy_net_app_guest_color(void) { return g.room.view.guest_color; }
int gy_net_app_local_ready(void)
{
    const GyNetSeat *s;
    if (g.active == 2) return g.local_ready;
    if (g.role != GY_NET_ROLE_HOST && g.role != GY_NET_ROLE_PLAYER) return 0;
    s = &g.room.seats[g.local_color];
    return s->ready;
}
int gy_net_app_countdown_seconds(void)
{
    uint32_t elapsed, remain;
    int s;
    if (g.room.view.phase != GY_NET_PHASE_STARTING) return 0;
    elapsed = (uint32_t)(g.now - g.room.view.phase_since_ms);
    if (elapsed >= GY_NET_START_MS) return 1;
    remain = GY_NET_START_MS - elapsed;
    s = (int)((remain + 999u) / 1000u);
    if (s < 1) s = 1;
    if (s > 3) s = 3;
    return s;
}
int gy_net_app_host_ready(void) { return g.room.view.host_ready; }
int gy_net_app_guest_ready(void) { return g.room.view.guest_ready; }
int gy_net_app_spectator_count(void) { return g.room.view.spectator_count; }
int gy_net_app_player_count(void) { return g.room.view.player_count; }
int gy_net_app_seq(void) { return (int)g.room.view.seq; }
int gy_net_app_local_id(void)
{
#ifdef GEYI_WITH_RTTHREAD
    return gy_net_ble_local_id();
#else
    return 0;
#endif
}
int gy_net_app_seat_occupied(int color)
{
    if (color != GY_NET_COLOR_RED && color != GY_NET_COLOR_BLUE) return 0;
    if (!g.active || g.room.view.phase == GY_NET_PHASE_CLOSED) return 0;
    if (color == g.room.view.host_color) return 1;
    if (color == g.room.view.guest_color) return g.room.view.player_count >= 2;
    return 0;
}

int gy_net_app_seat_ready(int color)
{
    if (!gy_net_app_seat_occupied(color)) return 0;
    /* 客户端刚点准备时房主快照还没回来，先用本地乐观值，避免按钮/方框
     * 先闪一下“未准备”；收到 ROOM_STATE 后以房主状态为准。 */
    if (g.active == 2 && color == g.local_color) return g.local_ready;
    if (color == g.room.view.host_color) return g.room.view.host_ready;
    if (color == g.room.view.guest_color) return g.room.view.guest_ready;
    return 0;
}

int gy_net_app_seat_id(int color)
{
    const GyNetSeat *s;
    if (color != GY_NET_COLOR_RED && color != GY_NET_COLOR_BLUE) return 0;
    if (g.active == 2 && color == g.local_color && g.local_player_id)
        return (int)g.local_player_id;
    s = &g.room.seats[color];
    if (s->player_id) return (int)s->player_id;
    if (color == g.room.view.host_color) return (int)g.room.view.host_id;
    if (color == g.room.view.guest_color) return (int)g.room.view.guest_id;
    return 0;
}
int gy_net_app_connected(void)
{
#ifdef GEYI_WITH_RTTHREAD
    if (g.active == 1) return gy_net_ble_conn_count() > 0;
    if (g.active == 2) return gy_net_ble_client_conn() != 0xff;
#endif
    return 0;
}
int gy_net_app_local_terminal(void) { return g.room.view.local_terminal; }
int gy_net_app_join_rejected(void) { return g.join_rejected; }
int gy_net_app_join_reason(void) { return g.join_reason; }
int gy_net_app_spectator_lost(void) { return g.spectator_lost; }
int gy_net_app_result(void) { return g.room.view.result; }
int gy_net_app_local_turn(void)
{
    if (g.role != GY_NET_ROLE_HOST && g.role != GY_NET_ROLE_PLAYER) return 0;
    if (g.room.view.phase != GY_NET_PHASE_PLAYING || !g.room.view.game_valid) return 0;
    return g.room.view.game.turn == g.local_color;
}
int gy_net_app_is_spectator(void) { return g.role == GY_NET_ROLE_SPECTATOR; }
int gy_net_app_pending_move(void) { return g.pending_move ? 1 : 0; }
const GyGame *gy_net_app_game(void) { return &g.room.view.game; }

int gy_net_app_lobby_start(void)
{
    /* 已经在大厅时不要 stop/start 扫描：两次快速点击“联机对战”会在
     * scan stop 未完成时再次 start，GAP 返回 67 后扫描停住，房间再也
     * 发现不了。只有真正在房间/对局里才需要先 leave。 */
    if (g.active) gy_net_app_leave();
    else {
        g.role = GY_NET_ROLE_SPECTATOR;
        g.local_color = GY_NET_COLOR_NONE;
        memset(&g.room, 0, sizeof(g.room));
    }
    g.room_count = 0;
    memset(g.rooms, 0, sizeof(g.rooms));
    g.join_rejected = 0;
    g.join_reason = GY_NET_JOIN_REASON_NONE;
    g.pending_join = 0;
    g.join_sent = 0;
    g.join_connected = 0;
    g.join_ready_at = 0;
    g.need_fresh_join = 0;
#ifdef GEYI_WITH_RTTHREAD
    /* 上一次加入失败/被关房时可能还留着一条 BLE 链路；不清掉会让
     * 对方仍认为有连接、停止广播，自己也扫不到房间。 */
    if (gy_net_ble_client_conn() != 0xff)
        gy_net_ble_disconnect(gy_net_ble_client_conn());
    gy_net_ble_scan_start();
#endif
    return 0;
}
void gy_net_app_lobby_stop(void)
{
#ifdef GEYI_WITH_RTTHREAD
    gy_net_ble_scan_stop();
#endif
    g.room_count = 0;
}
int gy_net_app_room_count(void) { return g.room_count; }
const GyNetAppRoom *gy_net_app_room_at(int index)
{
    if (index < 0 || index >= g.room_count) return NULL;
    return &g.rooms[index];
}

int gy_net_app_create_room(uint32_t room_id)
{
    gy_net_app_leave();
    if (!room_id) room_id = 0x1001u + (g.now & 0x0fffu);
    g.room_id = room_id;
    gy_net_room_init_host(&g.room, room_id, g.now, 2);
    {
        GyConfig cfg;
        if (gy_param_apply(GY_MODE_PVP, &cfg)) gy_net_room_set_config(&g.room, &cfg);
    }
    g.active = 1;
    g.role = GY_NET_ROLE_HOST;
    g.local_color = g.room.view.host_color;
    g.local_player_id = (uint16_t)gy_net_app_local_id();
    gy_net_room_set_player_id(&g.room, g.local_color, g.local_player_id);
    g.token = g.room.seats[g.local_color].token;
    g.last_adv = g.last_hb = g.now;
#ifdef GEYI_WITH_RTTHREAD
    gy_net_ble_host_start(room_id, g.room.view.player_count, g.room.view.phase,
                          g.room.view.spectator_count, g.room.view.max_spectators);
#endif
    return 0;
}

int gy_net_app_join_index(int index)
{
    return gy_net_app_join_index_role(index, -1);
}

int gy_net_app_join_index_role(int index, int role)
{
    const GyNetAppRoom *r = gy_net_app_room_at(index);
    GyNetRoomSummary summary;
    int join_role;
    if (!r) {
        /* 点击瞬间房间已被 TTL 移除/列表缩位：提示不存在。 */
        g.join_rejected = 1;
        g.join_reason = GY_NET_JOIN_REASON_NOT_FOUND;
        return -1;
    }
    if ((uint32_t)(g.now - r->seen_ms) > APP_ROOM_FRESH_MS) {
        /* 摘要过期：房主很可能已经关房，先移除再提示，不要让用户
         * 连一个已经不存在的房间。 */
        uint32_t room_id = r->room_id;
        lobby_remove_room(room_id);
        g.join_rejected = 1;
        g.join_reason = GY_NET_JOIN_REASON_NOT_FOUND;
        return -1;
    }
    memset(&summary, 0, sizeof(summary));
    summary.valid = 1;
    summary.room_id = r->room_id;
    summary.players = r->players;
    summary.phase = r->phase;
    summary.spectators = r->spectators;
    summary.max_spectators = r->max_spectators;
    if (role == GY_NET_ROLE_SPECTATOR) {
        if (r->phase == GY_NET_PHASE_RESULT || r->phase == GY_NET_PHASE_CLOSED ||
            r->spectators >= r->max_spectators)
            join_role = -1;
        else
            join_role = GY_NET_ROLE_SPECTATOR;
    } else {
        join_role = gy_net_lobby_click(&summary, r->max_spectators);
    }
    if (join_role < 0) {
        uint8_t reason = (r->phase == GY_NET_PHASE_RESULT ||
                          r->phase == GY_NET_PHASE_CLOSED)
                         ? GY_NET_JOIN_REASON_NOT_FOUND
                         : GY_NET_JOIN_REASON_FULL;
        gy_net_app_leave();
        g.join_rejected = 1;
        g.join_reason = reason;
        return -1;
    }
    gy_net_app_leave();
    g.room_id = r->room_id;
    g.join_role = (uint8_t)join_role;
    /* 协议里 0=玩家、1=旁观、2=自动；本地 role 用 GY_NET_ROLE_*。 */
    g.join_req_role = (role < 0) ? 2 :
                      (role == GY_NET_ROLE_SPECTATOR) ? 1 : 0;
    memcpy(g.join_addr, r->addr, 6);
    g.join_addr_type = r->addr_type;
    g.local_player_id = (uint16_t)gy_net_app_local_id();
    gy_net_room_init_client(&g.room, r->room_id, g.now);
    g.active = 2;
    g.role = g.join_role;
    g.local_color = GY_NET_COLOR_NONE;
    g.pending_join = 1;
    g.join_sent = 0;
    g.join_at = g.now;
    g.join_connected = 0;
    g.join_ready_at = 0;
    g.join_rejected = 0;
    g.join_reason = GY_NET_JOIN_REASON_NONE;
    g.dropped = 0;
    g.last_rx = g.now;
#ifdef GEYI_WITH_RTTHREAD
    gy_net_ble_scan_stop();
    gy_net_ble_connect(g.join_addr, g.join_addr_type);
#endif
    return 0;
}

void gy_net_app_leave(void)
{
#ifdef GEYI_WITH_RTTHREAD
    /* 先丢弃上一房间的迟到 RX，避免换房后旧 ROOM_STATE/ACK 污染新会话。 */
    gy_net_ble_rx_flush();
    if (g.active == 1) {
        int closed = 0;
        if (g.room.view.phase == GY_NET_PHASE_WAIT) {
            /* 等待期退出：有对手则把房主转给他；没有对手则关房。
             * 先把新状态发给当前连接，再断开并停止广播。 */
            int rc = gy_net_room_leave(&g.room, g.room.view.host_color, g.now);
            if (rc != GY_NET_ERR_ARG) {
                g.room.view.host_ready = 0;
                g.room.view.guest_ready = 0;
                APP_LOG("leave host phase=%d players=%d hostc=%d seq=%u",
                        g.room.view.phase, g.room.view.player_count,
                        g.room.view.host_color, (unsigned)g.room.view.seq);
                broadcast_state();
                if (rc == GY_NET_LEAVE_CLOSED) closed = 1;
            }
        } else if (g.room.view.phase == GY_NET_PHASE_PLAYING) {
            /* 对局中主动退出 = 认输；先广播权威结果再断开。 */
            if (gy_net_room_resign(&g.room, g.local_color, g.now) == GY_NET_OK) {
                broadcast_result();
                broadcast_state();
            }
            g.room.view.phase = GY_NET_PHASE_CLOSED;
            g.room.view.player_count = 0;
            g.room.view.spectator_count = 0;
            g.room.view.seq++;
            broadcast_state();
            closed = 1;
        } else if (g.room.view.phase == GY_NET_PHASE_RESULT ||
                   g.room.view.phase == GY_NET_PHASE_STARTING) {
            /* 结算/倒计时阶段房主回大厅 = 解散，不转移房主。 */
            int rc = gy_net_room_leave(&g.room, g.room.view.host_color, g.now);
            if (rc != GY_NET_ERR_ARG) {
                broadcast_state();
                if (rc == GY_NET_LEAVE_CLOSED) closed = 1;
            }
        }
        if (closed) {
            /* 让还在大厅扫描的板子立刻收到 players=0/CLOSED 摘要，
             * 不用等 1.5s TTL，也避免“关房后房间还挂几秒”。 */
            host_update_adv();
            rt_thread_mdelay(200);
        }
        {
            uint8_t conns[GY_NET_BLE_MAX_CONN];
            uint8_t i, n = gy_net_ble_conn_count();
            for (i = 0; i < n; ++i) conns[i] = gy_net_ble_conn_at(i);
            gy_net_ble_flush(300);
            rt_thread_mdelay(150);
            for (i = 0; i < n; ++i) gy_net_ble_disconnect(conns[i]);
        }
        gy_net_ble_host_stop();
        /* stop 与断开之间仍可能挤进一个连接；再清一次。 */
        {
            uint8_t conns2[GY_NET_BLE_MAX_CONN];
            uint8_t i2, n2 = gy_net_ble_conn_count();
            for (i2 = 0; i2 < n2; ++i2) conns2[i2] = gy_net_ble_conn_at(i2);
            for (i2 = 0; i2 < n2; ++i2) gy_net_ble_disconnect(conns2[i2]);
        }
    } else if (g.active == 2) {
        uint8_t conn = gy_net_ble_client_conn();
        if (conn != 0xff) {
            GyNetLeave l;
            GyNetMsg m;
            l.reason = (g.room.view.phase == GY_NET_PHASE_PLAYING)
                       ? GY_NET_REASON_RESIGN : GY_NET_REASON_LEFT;
            if (gy_net_pack_leave(&m, &l) == GY_NET_OK) send_msg(conn, &m);
            /* 给 LEAVE 足够时间发出，否则房主只能等 10s 断线窗口。 */
            gy_net_ble_flush(500);
            rt_thread_mdelay(100);
            gy_net_ble_disconnect(conn);
        }
    }
    gy_net_ble_scan_stop();
#endif
    g.active = 0;
    g.role = GY_NET_ROLE_SPECTATOR;
    g.local_color = GY_NET_COLOR_NONE;
    g.room_id = 0;
    g.token = 0;
    g.pending_join = 0;
    g.join_sent = 0;
    g.join_connected = 0;
    g.join_ready_at = 0;
    g.pending_move = 0;
    g.placing = 0;
    g.place_col = -1;
    g.last_preview_valid = 0;
    g.local_ready = 0;
    g.ready_pending = 0;
    g.ready_desired = 0;
    g.need_fresh_join = 0;
    g.starting_seq = 0;
    g.join_rejected = 0;
    g.spectator_lost = 0;
    g.dropped = 0;
    g.drop_at = 0;
    g.reconnect_at = 0;
    g.rejoin_sent = 0;
    g.preview_valid[0] = g.preview_valid[1] = 0;
    memset(&g.room, 0, sizeof(g.room));
    memset(g.conns, 0, sizeof(g.conns));
}

int gy_net_app_set_ready(int ready)
{
    if (g.active == 1) {
        if (gy_net_room_set_ready(&g.room, g.local_color, ready, g.now) != GY_NET_OK) return 0;
        broadcast_state();
        host_update_adv();
        return 1;
    }
    if (g.active == 2 && g.role == GY_NET_ROLE_PLAYER) {
        GyNetReady r;
        GyNetMsg m;
        r.ready = ready ? 1 : 0;
        if (gy_net_pack_ready(&m, &r) == GY_NET_OK) {
            send_msg(0xff, &m);
            g.local_ready = r.ready;
            g.ready_desired = r.ready;
            g.ready_pending = 1;
            g.ready_at = g.now;
            return 1;
        }
    }
    return 0;
}

int gy_net_app_swap(void)
{
    int i;
    if (g.active != 1 || g.role != GY_NET_ROLE_HOST) return 0;
    if (gy_net_room_swap_seats(&g.room) != GY_NET_OK) return 0;
    g.local_color = g.room.view.host_color;
    for (i = 0; i < APP_MAX_CONN; ++i) {
        if (!g.conns[i].used || g.conns[i].conn == 0xff) continue;
        g.conns[i].color = conn_color(&g.conns[i]);
        if (color_valid(g.conns[i].color))
            send_join_ack(g.conns[i].conn, g.conns[i].color, g.conns[i].token,
                          gy_net_room_seat_id(&g.room, g.conns[i].color));
    }
    g.reack_left = 3;   /* 1s 一次，共 3 次；重复 ACK 幂等 */
    g.reack_at = g.now;
    broadcast_state();
    return 1;
}

int gy_net_app_preview(int direction, int count)
{
    GyNetPreview p;
    if (!gy_net_app_local_turn()) return 0;
    if (g.last_preview_valid && g.last_dir == direction && g.last_count == count) return 0;
    g.last_preview_valid = 1;
    g.last_dir = direction;
    g.last_count = count;
    g.dir = direction;
    g.count = count;
    memset(&p, 0, sizeof(p));
    p.side = g.local_color;
    p.direction = (uint8_t)direction;
    p.count = (uint8_t)count;
    p.indicator = (uint16_t)((direction & 0xff) | ((count & 0xff) << 8));
    p.seq = g.room.view.seq;
    g.preview[p.side] = p;
    g.preview_valid[p.side] = 1;
    if (g.active == 1) {
        GyNetMsg m;
        if (gy_net_room_preview(&g.room, &p) > 0 && gy_net_pack_preview(&m, &p) == GY_NET_OK)
            send_spectators(&m);
    } else if (g.active == 2) {
        GyNetMsg m;
        if (gy_net_pack_preview(&m, &p) == GY_NET_OK) send_msg(0xff, &m);
    }
    return 1;
}

int gy_net_app_confirm(void)
{
    if (g.active == 1) {
        int rc;
        if (g.placing) {
            if (g.place_col < 0) return 0;
            rc = gy_net_room_place(&g.room, g.local_color, g.place_col, g.now);
        } else {
            rc = gy_net_room_move(&g.room, g.local_color, g.dir, g.count, g.now);
        }
        if (rc != GY_NET_OK) return 0;
        gy_net_room_preview_clear(&g.room, g.local_color);
        send_preview_clear_to_spectators(g.local_color);
        g.preview_valid[g.local_color] = 0;
        g.last_preview_valid = 0;
        g.placing = 0;
        broadcast_snapshot();
        host_phase_change(GY_NET_PHASE_PLAYING);
        return 1;
    }
    if (g.active == 2 && g.role == GY_NET_ROLE_PLAYER) {
        GyNetMoveReq mr;
        GyNetMsg m;
        if (g.pending_move) return 0;
        if (g.placing && g.place_col < 0) return 0;
        memset(&mr, 0, sizeof(mr));
        mr.side = g.local_color;
        mr.kind = g.placing ? 1 : 0;
        mr.direction = (uint8_t)g.dir;
        mr.count = (uint8_t)g.count;
        mr.column = (uint8_t)g.place_col;
        mr.seq = g.room.view.seq;
        if (gy_net_pack_move_req(&m, &mr) == GY_NET_OK) {
            send_msg(0xff, &m);
            g.pending_move = 1;
            g.placing = 0;
            return 1;
        }
    }
    return 0;
}

int gy_net_app_cancel(void)
{
    GyNetPreviewClear p;
    GyNetMsg m;
    memset(&p, 0, sizeof(p));
    p.side = g.local_color;
    if (color_valid(g.local_color)) {
        g.preview_valid[g.local_color] = 0;
        if (g.active == 1) {
            gy_net_room_preview_clear(&g.room, g.local_color);
            if (gy_net_pack_preview_clear(&m, &p) == GY_NET_OK) send_spectators(&m);
        } else if (g.active == 2 && g.role == GY_NET_ROLE_PLAYER) {
            if (gy_net_pack_preview_clear(&m, &p) == GY_NET_OK) send_msg(0xff, &m);
        }
    }
    g.placing = 0;
    g.place_col = -1;
    g.last_preview_valid = 0;
    return 1;
}

int gy_net_app_place(int column)
{
    g.placing = 1;
    g.place_col = column;
    return 1;
}

int gy_net_app_resign(void)
{
    if (g.active == 1) {
        if (gy_net_room_resign(&g.room, g.local_color, g.now) != GY_NET_OK) return 0;
        broadcast_result();
        broadcast_state();
        return 1;
    }
    if (g.active == 2 && g.role == GY_NET_ROLE_PLAYER) {
        GyNetLeave l;
        GyNetMsg m;
        l.reason = GY_NET_REASON_RESIGN;
        if (gy_net_pack_leave(&m, &l) == GY_NET_OK) { send_msg(0xff, &m); return 1; }
    }
    return 0;
}

int gy_net_app_preview_visible(int side)
{
    if (!color_valid((uint8_t)side) || !g.preview_valid[side]) return 0;
    if (g.role == GY_NET_ROLE_SPECTATOR) return 1;
    return g.local_color == side;
}
const GyNetPreview *gy_net_app_preview_at(int side)
{
    if (!color_valid((uint8_t)side)) return NULL;
    return &g.preview[side];
}

int gy_net_app_selftest(void)
{
    if (gy_net_proto_selftest() != 0) return -1;
    if (gy_net_room_selftest() != 0) return -1;
#ifdef GEYI_WITH_RTTHREAD
    if (gy_net_ble_selftest() != 0) return -1;
#endif
    return 0;
}
