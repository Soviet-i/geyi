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
#endif
#ifndef GY_NET_BLE_BROADCAST_CONN
#define GY_NET_BLE_BROADCAST_CONN 0xff
#endif

#define APP_MAX_CONN 4
#define APP_ROOM_TTL_MS 3000u
#define APP_HEARTBEAT_MS 1000u
/* 首次加入要等服务发现 + CCCD 写入；5s 在板端太短。 */
#define APP_JOIN_TIMEOUT_MS 12000u

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
    uint8_t  join_role;
    uint8_t  join_addr[6];
    uint8_t  join_addr_type;
    uint8_t  join_rejected;
    uint8_t  pending_move;
    uint8_t  local_ready;
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
static void send_join_ack(uint8_t conn, uint8_t color, uint32_t token)
{
    GyNetJoinAck a;
    GyNetMsg m;
    memset(&a, 0, sizeof(a));
    a.color = color;
    a.seat = color == GY_NET_COLOR_RED ? 0 : color == GY_NET_COLOR_BLUE ? 1 : 0;
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
    for (i = 0; i < 2; ++i)
        if (g.room.seats[i].token && g.room.seats[i].token == c->token)
            return (uint8_t)i;
    return c->color;
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

static void host_sync_conns(void)
{
#ifdef GEYI_WITH_RTTHREAD
    int i;
    for (i = 0; i < APP_MAX_CONN; ++i) {
        if (!g.conns[i].used || g.conns[i].conn == 0xff) continue;
        if (!ble_has_conn(g.conns[i].conn) ||
            (uint32_t)(g.now - g.conns[i].last_rx) >= 10000u) {
            gy_net_room_disconnect_token(&g.room, g.conns[i].token, g.now);
            g.conns[i].conn = 0xff;
        }
    }
#endif
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
                send_join_ack(conn, conn_color(existing), existing->token);
                send_state_to(conn);
                if (g.room.view.game_valid) send_snapshot_to(conn);
                return;
            }
        }
        room_role = (j.role == 0) ? GY_NET_ROLE_PLAYER : GY_NET_ROLE_SPECTATOR;
        if (gy_net_room_join(&g.room, room_role, g.now, &token, &color) != GY_NET_OK) {
            send_join_reject(conn, GY_NET_ERR_FULL);
            return;
        }
        conn_alloc(conn, room_role, color, token);
        send_join_ack(conn, color, token);
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
                    send_join_ack(g.conns[i].conn, conn_color(&g.conns[i]), g.conns[i].token);
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
            if (g.room.view.phase == GY_NET_PHASE_PLAYING && color_valid(color))
                gy_net_room_resign(&g.room, color, g.now);
            else
                gy_net_room_leave_token(&g.room, c->token, g.now);
        }
        c->used = 0;
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
        send_join_ack(conn, color, r.token);
        send_state_to(conn);
        send_snapshot_to(conn);
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

static void client_process(const GyNetMsg *msg)
{
    g.last_rx = g.now;
    switch (msg->type) {
    case GY_NET_JOIN_ACK: {
        GyNetJoinAck a;
        if (gy_net_unpack_join_ack(msg, &a) != GY_NET_OK) return;
        if (a.color != GY_NET_COLOR_NONE &&
            a.color != GY_NET_COLOR_RED && a.color != GY_NET_COLOR_BLUE) return;
        g.token = a.token;
        g.local_color = a.color;
        g.role = a.color == GY_NET_COLOR_NONE ? GY_NET_ROLE_SPECTATOR : GY_NET_ROLE_PLAYER;
        g.room.role = g.role;
        g.room.local_color = a.color;
        g.room.token = a.token;
        g.join_rejected = 0;
        g.pending_join = 0;
        g.join_sent = 0;
        g.local_ready = 0;
        g.dropped = 0;
        g.rejoin_sent = 0;
        g.drop_at = 0;
        gy_net_room_client_reconnect(&g.room, g.now);
        g.last_rx = g.now;
        break;
    }
    case GY_NET_JOIN_REJECT:
        g.join_rejected = 1;
        g.pending_join = 0;
        g.join_sent = 0;
        g.active = 0;
        break;
    case GY_NET_ROOM_STATE: {
        GyNetRoomState s;
        if (gy_net_unpack_room_state(msg, &s) != GY_NET_OK) return;
        if (s.phase == GY_NET_PHASE_CLOSED) {
            g.active = 0;
            g.role = GY_NET_ROLE_SPECTATOR;
#ifdef GEYI_WITH_RTTHREAD
            if (gy_net_ble_client_conn() != 0xff)
                gy_net_ble_disconnect(gy_net_ble_client_conn());
#endif
            return;
        }
        if (g.role == GY_NET_ROLE_PLAYER && g.local_color == s.host_color &&
            s.player_count == 1 && s.phase == GY_NET_PHASE_WAIT) {
            /* 等待阶段原房主退出：本机接管房间，开始广播。 */
            uint32_t token = g.token;
            GyConfig cfg;
            gy_net_room_promote_client(&g.room, s.host_color, g.now);
            if (gy_param_apply(GY_MODE_PVP, &cfg)) gy_net_room_set_config(&g.room, &cfg);
            g.room.seats[s.host_color].token = token;
            g.token = token;
            g.active = 1;
            g.role = GY_NET_ROLE_HOST;
            g.local_color = s.host_color;
            g.local_ready = 0;
            g.pending_join = 0;
            g.pending_move = 0;
            g.preview_valid[0] = g.preview_valid[1] = 0;
            g.last_preview_valid = 0;
#ifdef GEYI_WITH_RTTHREAD
            gy_net_ble_scan_stop();
            if (gy_net_ble_client_conn() != 0xff)
                gy_net_ble_disconnect(gy_net_ble_client_conn());
            gy_net_ble_host_start(g.room_id, 1, GY_NET_PHASE_WAIT, 0, 2);
#endif
            return;
        }
        gy_net_room_apply_room_state(&g.room, &s);
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
    case GY_NET_REJECT:
        g.pending_move = 0;
        g.last_preview_valid = 0;
        if (color_valid(g.local_color)) g.preview_valid[g.local_color] = 0;
        break;
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

static void lobby_drain(void)
{
#ifdef GEYI_WITH_RTTHREAD
    GyNetBleAdv adv;
    int i, slot;
    while (gy_net_ble_adv_recv(&adv)) {
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
            else if (g.active == 2) client_process(&rx.msg);
        }
    }
#endif
    if (g.active == 1) {
        int old_phase;
        host_sync_conns();
        old_phase = g.room.view.phase;
        gy_net_room_tick(&g.room, now_ms);
        host_phase_change(old_phase);
        if (g.room.view.phase == GY_NET_PHASE_CLOSED) {
            gy_net_app_leave();
            return;
        }
        if ((uint32_t)(now_ms - g.last_hb) >= APP_HEARTBEAT_MS) {
            GyNetMsg m;
            g.last_hb = now_ms;
            gy_net_msg_init(&m, GY_NET_HEARTBEAT, g.room.view.seq);
            send_broadcast(&m);
        }
        if ((uint32_t)(now_ms - g.last_adv) >= APP_HEARTBEAT_MS) {
            g.last_adv = now_ms;
            host_update_adv();
        }
    } else if (g.active == 2) {
#ifdef GEYI_WITH_RTTHREAD
        uint8_t conn = gy_net_ble_client_conn();
        if (g.pending_join && conn == 0xff &&
            (uint32_t)(now_ms - g.reconnect_at) >= 1000u) {
            g.reconnect_at = now_ms;
            gy_net_ble_connect(g.join_addr, g.join_addr_type);
        }
        if (g.pending_join && !g.join_sent && gy_net_ble_ready()) {
            GyNetJoinReq j;
            GyNetMsg m;
            j.role = g.join_role == GY_NET_ROLE_SPECTATOR ? 1 : 0;
            j.room_id = g.room_id;
            if (gy_net_pack_join_req(&m, &j) == GY_NET_OK) {
                send_msg(conn, &m);
                g.join_sent = 1;
                g.join_sent_at = now_ms;
                g.last_rx = now_ms;
            }
        }
        /* JOIN_ACK 可能因 CCCD 尚未生效而丢失；1s 未收到 ACK 就重发。 */
        if (g.pending_join && g.join_sent &&
            (uint32_t)(now_ms - g.join_sent_at) >= 1000u) {
            g.join_sent = 0;
        }
        if (g.pending_join && (uint32_t)(now_ms - g.join_at) >= APP_JOIN_TIMEOUT_MS) {
            g.join_rejected = 1;
            g.pending_join = 0;
            g.join_sent = 0;
            g.active = 0;
        }
        if (conn == 0xff && !g.dropped) {
            g.dropped = 1;
            g.drop_at = now_ms;
            gy_net_room_client_disconnect(&g.room, now_ms);
        }
        if (!g.dropped && (uint32_t)(now_ms - g.last_rx) >= 10000u) {
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
    if (color == g.room.view.host_color) return 1;
    if (color == g.room.view.guest_color) return g.room.view.player_count >= 2;
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
    gy_net_app_leave();
    g.room_count = 0;
    memset(g.rooms, 0, sizeof(g.rooms));
#ifdef GEYI_WITH_RTTHREAD
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
    const GyNetAppRoom *r = gy_net_app_room_at(index);
    GyNetRoomSummary summary;
    int join_role;
    if (!r) return -1;
    memset(&summary, 0, sizeof(summary));
    summary.valid = 1;
    summary.room_id = r->room_id;
    summary.players = r->players;
    summary.phase = r->phase;
    summary.spectators = r->spectators;
    summary.max_spectators = r->max_spectators;
    join_role = gy_net_lobby_click(&summary, r->max_spectators);
    if (join_role < 0) {
        gy_net_app_leave();
        g.join_rejected = 1;
        return -1;
    }
    gy_net_app_leave();
    g.room_id = r->room_id;
    g.join_role = (uint8_t)join_role;
    memcpy(g.join_addr, r->addr, 6);
    g.join_addr_type = r->addr_type;
    gy_net_room_init_client(&g.room, r->room_id, g.now);
    g.active = 2;
    g.role = g.join_role;
    g.local_color = GY_NET_COLOR_NONE;
    g.pending_join = 1;
    g.join_sent = 0;
    g.join_at = g.now;
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
    if (g.active == 1) {
        if (g.room.view.phase == GY_NET_PHASE_WAIT) {
            /* 等待期退出：有对手则把房主转给他；没有对手则关房。
             * 先把新状态发给当前连接，再断开并停止广播。 */
            if (gy_net_room_leave(&g.room, g.room.view.host_color, g.now) != GY_NET_ERR_ARG) {
                g.room.view.host_ready = 0;
                g.room.view.guest_ready = 0;
                broadcast_state();
            }
        } else if (g.room.view.phase == GY_NET_PHASE_PLAYING) {
            /* 对局中主动退出 = 认输；先广播权威结果再断开。 */
            if (gy_net_room_resign(&g.room, g.local_color, g.now) == GY_NET_OK) {
                broadcast_result();
                broadcast_state();
            }
        }
        {
            uint8_t conns[GY_NET_BLE_MAX_CONN];
            uint8_t i, n = gy_net_ble_conn_count();
            for (i = 0; i < n; ++i) conns[i] = gy_net_ble_conn_at(i);
            gy_net_ble_flush(200);
            for (i = 0; i < n; ++i) gy_net_ble_disconnect(conns[i]);
        }
        gy_net_ble_host_stop();
    } else if (g.active == 2) {
        uint8_t conn = gy_net_ble_client_conn();
        if (conn != 0xff) {
            GyNetLeave l;
            GyNetMsg m;
            l.reason = (g.room.view.phase == GY_NET_PHASE_PLAYING)
                       ? GY_NET_REASON_RESIGN : GY_NET_REASON_LEFT;
            if (gy_net_pack_leave(&m, &l) == GY_NET_OK) send_msg(conn, &m);
            gy_net_ble_flush(200);
            gy_net_ble_disconnect(conn);
        }
    }
    gy_net_ble_scan_stop();
#endif
    g.active = 0;
    g.role = GY_NET_ROLE_SPECTATOR;
    g.local_color = GY_NET_COLOR_NONE;
    g.pending_join = 0;
    g.join_sent = 0;
    g.pending_move = 0;
    g.placing = 0;
    g.place_col = -1;
    g.last_preview_valid = 0;
    g.local_ready = 0;
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
            send_join_ack(g.conns[i].conn, g.conns[i].color, g.conns[i].token);
    }
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
    return gy_net_room_selftest() == 0 ? 0 : -1;
}
