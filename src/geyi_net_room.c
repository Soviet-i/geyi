/*
 * geyi_net_room.c —— 联机房间纯逻辑状态机（REQ-501~REQ-511、REQ-513）
 *
 * 房主是唯一权威：只有房主侧调用 gy_net_room_move/place/resign 结算；
 * 客方只 apply_snapshot/apply_room_state，不本地提交。所有时间由调用者传入，
 * 便于主机用固定时钟测试 3s 倒计时、10s 断线窗口和结算保留。
 */
#include "geyi_net_room.h"
#include <string.h>

static int color_valid(uint8_t color)
{
    return color == GY_NET_COLOR_RED || color == GY_NET_COLOR_BLUE;
}

static GyNetSeat *seat_at(GyNetRoom *room, uint8_t color)
{
    if (!room || !color_valid(color)) return NULL;
    return &room->seats[color];
}
static const GyNetSeat *seat_at_const(const GyNetRoom *room, uint8_t color)
{
    if (!room || !color_valid(color)) return NULL;
    return &room->seats[color];
}

static uint32_t make_token(GyNetRoom *room)
{
    room->next_token++;
    if (!room->next_token) room->next_token = 1;
    return room->next_token;
}

void gy_net_room_set_player_id(GyNetRoom *room, uint8_t color, uint16_t player_id)
{
    GyNetSeat *s;
    if (!room || !color_valid(color)) return;
    s = seat_at(room, color);
    if (!s) return;
    s->player_id = player_id;
    if (color == room->view.host_color) room->view.host_id = player_id;
    if (color == room->view.guest_color) room->view.guest_id = player_id;
}

uint16_t gy_net_room_seat_id(const GyNetRoom *room, uint8_t color)
{
    const GyNetSeat *s;
    if (!room || !color_valid(color)) return 0;
    s = seat_at_const(room, color);
    return s ? s->player_id : 0;
}

static void sync_ready(GyNetRoom *room)
{
    const GyNetSeat *h = seat_at_const(room, room->view.host_color);
    const GyNetSeat *g = seat_at_const(room, room->view.guest_color);
    room->view.host_ready = h ? h->ready : 0;
    room->view.guest_ready = g ? g->ready : 0;
}

static void release_guest(GyNetRoom *room)
{
    GyNetSeat *g;
    if (!color_valid(room->view.guest_color)) return;
    g = seat_at(room, room->view.guest_color);
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->role = GY_NET_ROLE_PLAYER;
    g->color = room->view.guest_color;
    room->view.player_count = room->view.host_color == room->view.guest_color ? 0 : 1;
    room->view.guest_ready = 0;
    room->view.guest_id = 0;
}

static void release_spectator(GyNetRoom *room, int index)
{
    int i;
    if (index < 0 || index >= GY_NET_MAX_SPECTATORS) return;
    for (i = index; i + 1 < GY_NET_MAX_SPECTATORS; ++i) {
        room->spectators[i] = room->spectators[i + 1];
        room->spectator_disconnect_ms[i] = room->spectator_disconnect_ms[i + 1];
    }
    memset(&room->spectators[GY_NET_MAX_SPECTATORS - 1], 0,
           sizeof(room->spectators[0]));
    room->spectator_disconnect_ms[GY_NET_MAX_SPECTATORS - 1] = 0;
    if (room->view.spectator_count) room->view.spectator_count--;
}

static void set_result(GyNetRoom *host, uint8_t outcome, uint8_t reason, uint32_t now_ms)
{
    host->view.phase = GY_NET_PHASE_RESULT;
    host->view.result = outcome;
    host->view.result_reason = reason;
    host->view.phase_since_ms = now_ms;
    host->seats[host->view.host_color].ready = 0;
    host->seats[host->view.guest_color].ready = 0;
    sync_ready(host);
    host->view.seq++;
}

void gy_net_room_init_host(GyNetRoom *room, uint32_t room_id, uint32_t now_ms,
                           uint8_t max_spectators)
{
    if (!room) return;
    memset(room, 0, sizeof(*room));
    room->view.room_id = room_id;
    room->view.phase = GY_NET_PHASE_WAIT;
    room->view.host_color = GY_NET_COLOR_RED;
    room->view.guest_color = GY_NET_COLOR_BLUE;
    room->view.player_count = 1;
    room->view.max_spectators = max_spectators;
    room->view.seq = 0;
    room->view.phase_since_ms = now_ms;
    room->role = GY_NET_ROLE_HOST;
    room->local_color = GY_NET_COLOR_RED;
    room->connected = 1;
    room->now_ms = now_ms;
    room->next_token = room_id ? room_id : 1;
    room->config = gy_default_config();
    room->seats[GY_NET_COLOR_RED].role = GY_NET_ROLE_HOST;
    room->seats[GY_NET_COLOR_RED].color = GY_NET_COLOR_RED;
    room->seats[GY_NET_COLOR_RED].connected = 1;
    room->seats[GY_NET_COLOR_RED].token = make_token(room);
    room->seats[GY_NET_COLOR_RED].last_seq = 0;
    room->seats[GY_NET_COLOR_RED].player_id = 0;
    room->view.host_id = 0;
    room->view.guest_id = 0;
    room->seats[GY_NET_COLOR_BLUE].role = GY_NET_ROLE_PLAYER;
    room->seats[GY_NET_COLOR_BLUE].color = GY_NET_COLOR_BLUE;
    room->seats[GY_NET_COLOR_BLUE].connected = 0;
    room->token = room->seats[GY_NET_COLOR_RED].token;
}

void gy_net_room_init_client(GyNetRoom *room, uint32_t room_id, uint32_t now_ms)
{
    if (!room) return;
    memset(room, 0, sizeof(*room));
    room->view.room_id = room_id;
    room->view.phase = GY_NET_PHASE_WAIT;
    room->view.host_color = GY_NET_COLOR_RED;
    room->view.guest_color = GY_NET_COLOR_BLUE;
    room->view.player_count = 1;
    room->role = GY_NET_ROLE_PLAYER;
    room->local_color = GY_NET_COLOR_NONE;
    room->connected = 1;
    room->now_ms = now_ms;
    room->config = gy_default_config();
}

int gy_net_room_set_config(GyNetRoom *room, const GyConfig *config)
{
    GyGame probe;
    if (!room || !config) return GY_NET_ERR_ARG;
    if (!gy_init(&probe, *config, 1)) return GY_NET_ERR_RANGE;
    room->config = *config;
    return GY_NET_OK;
}

int gy_net_room_promote_client(GyNetRoom *client, uint8_t new_host_color, uint32_t now_ms)
{
    uint8_t other;
    if (!client || !color_valid(new_host_color)) return GY_NET_ERR_ARG;
    other = new_host_color == GY_NET_COLOR_RED ? GY_NET_COLOR_BLUE : GY_NET_COLOR_RED;
    client->role = GY_NET_ROLE_HOST;
    client->local_color = new_host_color;
    client->connected = 1;
    client->view.host_color = new_host_color;
    client->view.guest_color = other;
    client->view.phase = GY_NET_PHASE_WAIT;
    client->view.phase_since_ms = now_ms;
    client->view.player_count = 1;
    client->view.max_spectators = 2;
    client->view.host_ready = 0;
    client->view.guest_ready = 0;
    client->view.host_id = 0;   /* 由 app 在接管后写入本机 ID */
    client->view.guest_id = 0;
    client->view.game_valid = 0;
    client->view.result = GY_PLAYING;
    client->view.result_reason = GY_NET_REASON_NORMAL;
    client->view.local_terminal = 0;
    client->view.seat_reconnect_s[0] = 0;
    client->view.seat_reconnect_s[1] = 0;
    client->view.spectator_count = 0;
    client->view.seq++;
    memset(client->spectators, 0, sizeof(client->spectators));
    memset(client->spectator_disconnect_ms, 0, sizeof(client->spectator_disconnect_ms));
    if (client->next_token <= client->token) client->next_token = client->token + 1;
    memset(&client->seats[0], 0, sizeof(client->seats[0]));
    memset(&client->seats[1], 0, sizeof(client->seats[1]));
    client->seats[new_host_color].role = GY_NET_ROLE_HOST;
    client->seats[new_host_color].color = new_host_color;
    client->seats[new_host_color].connected = 1;
    client->seats[new_host_color].token = client->token;
    client->seats[other].role = GY_NET_ROLE_PLAYER;
    client->seats[other].color = other;
    return GY_NET_OK;
}

int gy_net_room_join(GyNetRoom *host, uint8_t role, uint16_t player_id,
                     uint32_t now_ms, uint32_t *token_out, uint8_t *color_out)
{
    uint32_t token;
    if (!host || !token_out || !color_out) return GY_NET_ERR_ARG;
    host->now_ms = now_ms;
    if (role == GY_NET_ROLE_PLAYER) {
        GyNetSeat *g;
        if (host->view.phase != GY_NET_PHASE_WAIT) return GY_NET_ERR_STATE;
        if (host->view.player_count >= 2) return GY_NET_ERR_FULL;
        g = seat_at(host, host->view.guest_color);
        if (!g || g->connected) return GY_NET_ERR_FULL;
        token = make_token(host);
        memset(g, 0, sizeof(*g));
        g->role = GY_NET_ROLE_PLAYER;
        g->color = host->view.guest_color;
        g->connected = 1;
        g->player_id = player_id;
        g->token = token;
        host->view.player_count = 2;
        host->view.guest_ready = 0;
        host->view.guest_id = player_id;
        host->view.seq++;
        *token_out = token;
        *color_out = g->color;
        return GY_NET_OK;
    }
    if (role == GY_NET_ROLE_SPECTATOR) {
        int i;
        if (host->view.phase == GY_NET_PHASE_RESULT ||
            host->view.phase == GY_NET_PHASE_CLOSED) return GY_NET_ERR_STATE;
        if (host->view.spectator_count >= GY_NET_MAX_SPECTATORS ||
            host->view.spectator_count >= host->view.max_spectators)
            return GY_NET_ERR_FULL;
        for (i = 0; i < GY_NET_MAX_SPECTATORS; ++i) {
            if (!host->spectators[i].token) break;
            if (!host->spectators[i].connected &&
                host->spectator_disconnect_ms[i] &&
                (uint32_t)(now_ms - host->spectator_disconnect_ms[i]) >= GY_NET_RECONNECT_MS) {
                break;
            }
        }
        if (i >= GY_NET_MAX_SPECTATORS) return GY_NET_ERR_FULL;
        {
            uint8_t reused = host->spectators[i].token ? 1 : 0;
            token = make_token(host);
            host->spectators[i].role = GY_NET_ROLE_SPECTATOR;
            host->spectators[i].color = GY_NET_COLOR_NONE;
            host->spectators[i].connected = 1;
            host->spectators[i].token = token;
            host->spectator_disconnect_ms[i] = 0;
            if (!reused) host->view.spectator_count++;
        }
        host->view.seq++;
        *token_out = token;
        *color_out = GY_NET_COLOR_NONE;
        return GY_NET_OK;
    }
    return GY_NET_ERR_ARG;
}

int gy_net_room_set_ready(GyNetRoom *host, uint8_t color, int ready, uint32_t now_ms)
{
    GyNetSeat *s;
    if (!host) return GY_NET_ERR_ARG;
    if (host->view.phase != GY_NET_PHASE_WAIT &&
        host->view.phase != GY_NET_PHASE_STARTING) return GY_NET_ERR_STATE;
    s = seat_at(host, color);
    if (!s || !s->connected ||
        (s->role != GY_NET_ROLE_HOST && s->role != GY_NET_ROLE_PLAYER))
        return GY_NET_ERR_STATE;
    ready = ready ? 1 : 0;
    if (s->ready == (uint8_t)ready) return GY_NET_OK;
    s->ready = (uint8_t)ready;
    host->now_ms = now_ms;
    sync_ready(host);
    if (host->view.host_ready && host->view.guest_ready &&
        host->seats[host->view.host_color].connected &&
        host->seats[host->view.guest_color].connected) {
        host->view.phase = GY_NET_PHASE_STARTING;
        host->view.phase_since_ms = now_ms;
    } else if (host->view.phase == GY_NET_PHASE_STARTING) {
        host->view.phase = GY_NET_PHASE_WAIT;
        host->view.phase_since_ms = now_ms;
    }
    host->view.seq++;
    return GY_NET_OK;
}

int gy_net_room_swap_seats(GyNetRoom *host)
{
    GyNetSeat tmp;
    uint8_t old_host, old_guest;
    if (!host) return GY_NET_ERR_ARG;
    if (host->view.phase != GY_NET_PHASE_WAIT ||
        host->view.player_count < 1 || host->view.player_count > 2)
        return GY_NET_ERR_STATE;
    old_host = host->view.host_color;
    old_guest = host->view.guest_color;
    if (!color_valid(old_host) || !color_valid(old_guest) ||
        host->seats[old_host].ready || host->seats[old_guest].ready)
        return GY_NET_ERR_STATE;
    /* 断线保留期间不能换座：否则 seat_disconnect_ms 与席位颜色错位，
     * 掉线席位会永远不被 WAIT tick 释放，reclaim 也会绕过 10s。 */
    if ((host->seats[old_host].token && !host->seats[old_host].connected) ||
        (host->seats[old_guest].token && !host->seats[old_guest].connected))
        return GY_NET_ERR_STATE;
    tmp = host->seats[old_host];
    host->seats[old_host] = host->seats[old_guest];
    host->seats[old_guest] = tmp;
    {
        uint32_t tmp_ms = host->seat_disconnect_ms[old_host];
        host->seat_disconnect_ms[old_host] = host->seat_disconnect_ms[old_guest];
        host->seat_disconnect_ms[old_guest] = tmp_ms;
    }
    host->seats[old_host].color = old_host;
    host->seats[old_guest].color = old_guest;
    host->view.host_color = old_guest;
    host->view.guest_color = old_host;
    if (host->role == GY_NET_ROLE_HOST) host->local_color = host->view.host_color;
    else if (host->role == GY_NET_ROLE_PLAYER) host->local_color = host->view.guest_color;
    sync_ready(host);
    host->view.seq++;
    return GY_NET_OK;
}

int gy_net_room_tick(GyNetRoom *room, uint32_t now_ms)
{
    int changed = 0, i;
    if (!room) return 0;
    room->now_ms = now_ms;

    if (room->view.phase == GY_NET_PHASE_STARTING) {
        if (!room->seats[room->view.guest_color].connected) {
            /* 倒计时中断线：取消开局回 WAIT，但保留席位 10s 等重连；
             * 超时由 WAIT 分支释放。不要立即抹成 1/2。 */
            room->view.phase = GY_NET_PHASE_WAIT;
            room->view.phase_since_ms = now_ms;
            sync_ready(room);
            room->view.seq++;
            return 1;
        }
        if ((uint32_t)(now_ms - room->view.phase_since_ms) >= GY_NET_START_MS) {
            if (!gy_init(&room->view.game, room->config,
                         room->view.room_id ? room->view.room_id : 1))
                return 0;
            room->view.phase = GY_NET_PHASE_PLAYING;
            room->view.phase_since_ms = now_ms;
            room->view.game_valid = 1;
            /* 开局后清 ready；结算回房间时双方必须重新准备。 */
            room->seats[room->view.host_color].ready = 0;
            room->seats[room->view.guest_color].ready = 0;
            sync_ready(room);
            room->view.seq++;
            changed = 1;
        }
    } else if (room->view.phase == GY_NET_PHASE_PLAYING) {
        for (i = 0; i < 2; ++i) {
            GyNetSeat *s = &room->seats[i];
            if (s->connected || s->role == GY_NET_ROLE_SPECTATOR) continue;
            if (!room->seat_disconnect_ms[i]) continue;
            if ((uint32_t)(now_ms - room->seat_disconnect_ms[i]) >= GY_NET_RECONNECT_MS) {
                uint8_t outcome = (i == GY_NET_COLOR_RED) ? GY_BLUE_WIN : GY_RED_WIN;
                set_result(room, outcome, GY_NET_REASON_DISCONNECT, now_ms);
                changed = 1;
                break;
            }
        }
    } else if (room->view.phase == GY_NET_PHASE_WAIT) {
        GyNetSeat *g = seat_at(room, room->view.guest_color);
        if (g && !g->connected && g->token &&
            room->seat_disconnect_ms[room->view.guest_color] &&
            (uint32_t)(now_ms - room->seat_disconnect_ms[room->view.guest_color]) >= GY_NET_RECONNECT_MS) {
            release_guest(room);
            room->view.seq++;
            changed = 1;
        }
    } else if (room->view.phase == GY_NET_PHASE_RESULT) {
        if ((uint32_t)(now_ms - room->view.phase_since_ms) >= GY_NET_RESULT_KEEP_MS) {
            /* 结算结束回房间，不关房：双方重新准备即可开下一局。 */
            room->view.phase = GY_NET_PHASE_WAIT;
            room->view.phase_since_ms = now_ms;
            room->view.result = GY_PLAYING;
            room->view.result_reason = GY_NET_REASON_NORMAL;
            room->view.game_valid = 0;
            room->seats[room->view.host_color].ready = 0;
            room->seats[room->view.guest_color].ready = 0;
            sync_ready(room);
            {
                GyNetSeat *guest = seat_at(room, room->view.guest_color);
                if (guest && guest->token && !guest->connected)
                    release_guest(room);
            }
            room->view.seq++;
            changed = 1;
        }
    }

    for (i = 0; i < GY_NET_MAX_SPECTATORS; ++i) {
        if (room->spectators[i].connected || !room->spectators[i].token) continue;
        if (!room->spectator_disconnect_ms[i]) continue;
        if ((uint32_t)(now_ms - room->spectator_disconnect_ms[i]) >= GY_NET_RECONNECT_MS) {
            release_spectator(room, i);
            room->view.seq++;
            changed = 1;
            --i;
        }
    }
    return changed;
}

int gy_net_room_move(GyNetRoom *host, uint8_t color, int direction, int count, int aim, uint32_t now_ms)
{
    GyMove move;
    GyPreview preview;
    if (!host) return GY_NET_ERR_ARG;
    if (host->view.phase != GY_NET_PHASE_PLAYING || !host->view.game_valid)
        return GY_NET_ERR_STATE;
    if (!color_valid(color) || color != (uint8_t)host->view.game.turn)
        return GY_NET_ERR_TURN;
    move.direction = direction;
    move.count = count;
    move.aim = aim;
    if (!gy_preview(&host->view.game, move, &preview)) return GY_NET_ERR_ACTION;
    if (!gy_commit(&host->view.game, &preview)) return GY_NET_ERR_ACTION;
    host->view.seq++;
    host->now_ms = now_ms;
    host->seats[color].last_seq = host->view.seq;
    if (host->view.game.outcome) {
        set_result(host, (uint8_t)host->view.game.outcome, GY_NET_REASON_NORMAL, now_ms);
    }
    return GY_NET_OK;
}

int gy_net_room_place(GyNetRoom *host, uint8_t color, int row, int column, uint32_t now_ms)
{
    if (!host) return GY_NET_ERR_ARG;
    if (host->view.phase != GY_NET_PHASE_PLAYING || !host->view.game_valid)
        return GY_NET_ERR_STATE;
    if (!color_valid(color) || color != (uint8_t)host->view.game.turn)
        return GY_NET_ERR_TURN;
    if (!gy_place_at(&host->view.game, row, column)) return GY_NET_ERR_ACTION;
    host->view.seq++;
    host->now_ms = now_ms;
    host->seats[color].last_seq = host->view.seq;
    if (host->view.game.outcome) {
        set_result(host, (uint8_t)host->view.game.outcome, GY_NET_REASON_NORMAL, now_ms);
    }
    return GY_NET_OK;
}

int gy_net_room_resign(GyNetRoom *host, uint8_t color, uint32_t now_ms)
{
    GyNetSeat *s;
    if (!host) return GY_NET_ERR_ARG;
    if (host->view.phase != GY_NET_PHASE_PLAYING) return GY_NET_ERR_STATE;
    s = seat_at(host, color);
    if (!s || (s->role != GY_NET_ROLE_HOST && s->role != GY_NET_ROLE_PLAYER))
        return GY_NET_ERR_ARG;
    set_result(host, color == GY_NET_COLOR_RED ? GY_BLUE_WIN : GY_RED_WIN,
               GY_NET_REASON_RESIGN, now_ms);
    return GY_NET_OK;
}

void gy_net_room_snapshot(const GyNetRoom *host, GyNetSnapshot *out)
{
    if (!host || !out) return;
    memset(out, 0, sizeof(*out));
    out->seq = host->view.seq;
    out->game = host->view.game;
}

int gy_net_room_apply_snapshot(GyNetRoom *client, const GyNetSnapshot *snapshot)
{
    if (!client || !snapshot) return GY_NET_ERR_ARG;
    if (snapshot->seq <= client->last_snapshot_seq) return 0;
    /* ROOM_STATE 的 seq 可能因准备/换座先于棋盘快照推进；如果快照携带
     * 更新的 game.revision（或客户端刚 START、棋盘尚无效），必须接受，
     * 否则客方会永远停在旧棋盘/pending。只有同 revision 的旧 seq 才拒绝。 */
    if (snapshot->seq < client->view.seq &&
        (!snapshot->game.config.size ||
         (client->view.game_valid &&
          snapshot->game.revision <= client->view.game.revision))) return 0;
    client->view.game = snapshot->game;
    client->view.game_valid = 1;
    /* 房间 seq 已可能被 ROOM_STATE/REJOIN 推高；快照的棋盘 revision
     * 更新时必须接受，但不能让 view.seq 因旧快照回退。 */
    if (snapshot->seq > client->view.seq) client->view.seq = snapshot->seq;
    client->last_snapshot_seq = snapshot->seq;
    /* 已收到 RESULT 后，旧棋盘快照（outcome=0）不能把结算页拉回 PLAYING。 */
    if (!(client->view.phase == GY_NET_PHASE_RESULT && !snapshot->game.outcome))
        client->view.phase = snapshot->game.outcome ? GY_NET_PHASE_RESULT : GY_NET_PHASE_PLAYING;
    client->view.result = (uint8_t)snapshot->game.outcome;
    client->view.local_terminal = 0;
    client->host_absent = 0;
    return 1;
}

static uint8_t seat_reconnect_seconds(const GyNetRoom *host, uint8_t color)
{
    const GyNetSeat *s;
    uint32_t elapsed;
    if (!host || !color_valid(color)) return 0;
    s = &host->seats[color];
    if (s->connected || !s->token || !host->seat_disconnect_ms[color]) return 0;
    elapsed = (uint32_t)(host->now_ms - host->seat_disconnect_ms[color]);
    if (elapsed >= GY_NET_RECONNECT_MS) return 0;
    return (uint8_t)((GY_NET_RECONNECT_MS - elapsed + 999u) / 1000u);
}

void gy_net_room_state(const GyNetRoom *host, GyNetRoomState *out)
{
    if (!host || !out) return;
    memset(out, 0, sizeof(*out));
    out->phase = host->view.phase;
    out->host_color = host->view.host_color;
    out->guest_color = host->view.guest_color;
    out->host_ready = host->view.host_ready;
    out->guest_ready = host->view.guest_ready;
    out->player_count = host->view.player_count;
    out->spectator_count = host->view.spectator_count;
    out->host_id = gy_net_room_seat_id(host, host->view.host_color);
    out->guest_id = gy_net_room_seat_id(host, host->view.guest_color);
    out->room_id = host->view.room_id;
    out->seq = host->view.seq;
    out->seat_online = (uint8_t)((host->seats[GY_NET_COLOR_RED].connected ? 1u : 0u) |
                                 (host->seats[GY_NET_COLOR_BLUE].connected ? 2u : 0u));
    out->seat_reconnect_s[GY_NET_COLOR_RED] =
        seat_reconnect_seconds(host, GY_NET_COLOR_RED);
    out->seat_reconnect_s[GY_NET_COLOR_BLUE] =
        seat_reconnect_seconds(host, GY_NET_COLOR_BLUE);
}

int gy_net_room_apply_room_state(GyNetRoom *client, const GyNetRoomState *state)
{
    int transient_changed = 0;
    int c;
    if (!client || !state) return GY_NET_ERR_ARG;
    if (state->seq < client->last_room_seq) return 0;
    /* 慢到的 ROOM_STATE 不能把已从 SNAPSHOT 前进的 phase/seq 拉回去。 */
    if (state->seq < client->view.seq) return 0;
    /* 同一 seq 的周期性 ROOM_STATE 只用于刷新断线倒计时/座位在线位；
     * 房主每秒重发一次，不能因为 seq 不变就把 10s 固定住。权威 phase/
     * ready/玩家 ID 仍只接受更高 seq。 */
    if (state->seq == client->last_room_seq && client->last_room_seq != 0) {
        for (c = 0; c < 2; ++c) {
            int online = (state->seat_online >> c) & 1;
            int sec = state->seat_reconnect_s[c];
            if (client->seats[c].connected != online) {
                client->seats[c].connected = (uint8_t)online;
                transient_changed = 1;
            }
            /* 只接受更小的倒计时；0 可能来自延迟包，恢复在线走更高 seq。 */
            if (sec && (client->view.seat_reconnect_s[c] == 0 ||
                        sec < client->view.seat_reconnect_s[c])) {
                client->view.seat_reconnect_s[c] = (uint8_t)sec;
                transient_changed = 1;
            }
        }
        return transient_changed;
    }
    if (!color_valid(state->host_color) || !color_valid(state->guest_color) ||
        state->host_color == state->guest_color ||
        state->phase > GY_NET_PHASE_CLOSED) return GY_NET_ERR_RANGE;
    client->view.phase = state->phase;
    client->view.host_color = state->host_color;
    client->view.guest_color = state->guest_color;
    client->view.host_ready = state->host_ready;
    client->view.guest_ready = state->guest_ready;
    client->view.player_count = state->player_count;
    client->view.spectator_count = state->spectator_count;
    client->view.host_id = state->host_id;
    client->view.guest_id = state->guest_id;
    client->seats[state->host_color].player_id = state->host_id;
    client->seats[state->guest_color].player_id = state->guest_id;
    client->seats[GY_NET_COLOR_RED].connected =
        (state->seat_online & 0x01u) ? 1 : 0;
    client->seats[GY_NET_COLOR_BLUE].connected =
        (state->seat_online & 0x02u) ? 1 : 0;
    client->view.seat_reconnect_s[GY_NET_COLOR_RED] = state->seat_reconnect_s[0];
    client->view.seat_reconnect_s[GY_NET_COLOR_BLUE] = state->seat_reconnect_s[1];
    client->view.room_id = state->room_id;
    client->view.seq = state->seq;
    client->last_room_seq = state->seq;
    client->view.local_terminal = 0;
    return 1;
}

int gy_net_room_apply_result(GyNetRoom *client, const GyNetResult *result)
{
    if (!client || !result) return GY_NET_ERR_ARG;
    if (result->outcome > GY_DRAW) return GY_NET_ERR_RANGE;
    if (result->seq && result->seq <= client->last_result_seq) return 0;
    if (result->seq && result->seq < client->view.seq) return 0;
    client->view.phase = GY_NET_PHASE_RESULT;
    client->view.result = result->outcome;
    client->view.result_reason = result->reason;
    if (result->seq) client->view.seq = result->seq;
    if (result->seq) client->last_result_seq = result->seq;
    client->view.local_terminal = 0;
    return 1;
}

int gy_net_room_preview(GyNetRoom *host, const GyNetPreview *preview)
{
    if (!host || !preview) return GY_NET_ERR_ARG;
    if (preview->side != GY_NET_COLOR_RED && preview->side != GY_NET_COLOR_BLUE)
        return GY_NET_ERR_RANGE;
    if (host->preview_valid[preview->side] &&
        host->preview[preview->side].direction == preview->direction &&
        host->preview[preview->side].count == preview->count &&
        host->preview[preview->side].column == preview->column &&
        host->preview[preview->side].row == preview->row &&
        host->preview[preview->side].aim == preview->aim &&
        host->preview[preview->side].indicator == preview->indicator)
        return 0;
    host->preview[preview->side] = *preview;
    host->preview_valid[preview->side] = 1;
    return 1;
}

int gy_net_room_preview_clear(GyNetRoom *host, uint8_t side)
{
    if (!host || side > GY_NET_COLOR_BLUE) return GY_NET_ERR_ARG;
    if (!host->preview_valid[side]) return 0;
    host->preview_valid[side] = 0;
    memset(&host->preview[side], 0, sizeof(host->preview[side]));
    return 1;
}

int gy_net_room_preview_visible(const GyNetRoom *host, uint8_t viewer_role,
                                uint8_t viewer_color, uint8_t side)
{
    if (!host || side > GY_NET_COLOR_BLUE) return 0;
    if (!host->preview_valid[side]) return 0;
    if (viewer_role == GY_NET_ROLE_SPECTATOR) return 1;
    return viewer_color == side;
}

int gy_net_room_disconnect_token(GyNetRoom *host, uint32_t token, uint32_t now_ms)
{
    int i;
    if (!host || !token) return GY_NET_ERR_ARG;
    host->now_ms = now_ms;
    for (i = 0; i < 2; ++i) {
        if (host->seats[i].token == token && host->seats[i].connected) {
            /* 链路断开（不是显式 LEAVE）一律保留席位 10s：房间页/对局页
             * 要显示“断线重连中”，重启或网络恢复后可以回到原席位。
             * 显式 LEAVE 走 gy_net_room_leave_token，仍然立即释放。 */
            host->seats[i].connected = 0;
            host->seats[i].ready = 0;      /* 断线后不能保留“已准备” */
            host->seat_disconnect_ms[i] = now_ms;
            sync_ready(host);
            host->view.seq++;
            return GY_NET_OK;
        }
    }
    for (i = 0; i < GY_NET_MAX_SPECTATORS; ++i) {
        if (host->spectators[i].token == token && host->spectators[i].connected) {
            if (host->view.phase == GY_NET_PHASE_WAIT) {
                release_spectator(host, i);
                host->view.seq++;
                return GY_NET_OK;
            }
            host->spectators[i].connected = 0;
            host->spectator_disconnect_ms[i] = now_ms;
            return GY_NET_OK;
        }
    }
    return GY_NET_ERR_TOKEN;
}

int gy_net_room_reclaim(GyNetRoom *host, uint16_t player_id, uint32_t now_ms,
                        uint32_t *token_out, uint8_t *color_out)
{
    int i;
    if (!host || !token_out || !color_out || !player_id) return GY_NET_ERR_ARG;
    if (host->view.phase == GY_NET_PHASE_CLOSED) return GY_NET_ERR_STATE;
    for (i = 0; i < 2; ++i) {
        GyNetSeat *s = &host->seats[i];
        if (i == host->view.host_color) continue;   /* 房主席位不回收 */
        if (!s->token || s->connected || s->player_id != player_id) continue;
        if (host->seat_disconnect_ms[i] &&
            (uint32_t)(now_ms - host->seat_disconnect_ms[i]) >= GY_NET_RECONNECT_MS)
            continue;
        s->token = make_token(host);
        s->connected = 1;
        s->ready = 0;                 /* 重启方必须重新准备 */
        host->seat_disconnect_ms[i] = 0;
        if (i == host->view.guest_color)
            host->view.guest_id = player_id;
        sync_ready(host);
        host->view.seq++;
        *token_out = s->token;
        *color_out = (uint8_t)i;
        return GY_NET_OK;
    }
    return GY_NET_ERR_TOKEN;
}

int gy_net_room_rejoin(GyNetRoom *host, uint32_t token, uint32_t last_seq,
                       uint32_t now_ms, uint8_t *color_out)
{
    int i;
    if (!host || !token || !color_out) return GY_NET_ERR_ARG;
    for (i = 0; i < 2; ++i) {
        if (host->seats[i].token != token) continue;
        if (host->seats[i].connected) return GY_NET_ERR_TOKEN;
        if (host->seat_disconnect_ms[i] &&
            (uint32_t)(now_ms - host->seat_disconnect_ms[i]) >= GY_NET_RECONNECT_MS)
            return GY_NET_ERR_TOKEN;
        host->seats[i].connected = 1;
        host->seats[i].ready = 0;
        host->seats[i].last_seq = last_seq;
        host->seat_disconnect_ms[i] = 0;
        sync_ready(host);
        host->now_ms = now_ms;
        host->view.seq++;
        *color_out = (uint8_t)i;
        return GY_NET_OK;
    }
    for (i = 0; i < GY_NET_MAX_SPECTATORS; ++i) {
        if (host->spectators[i].token != token) continue;
        if (host->spectators[i].connected) return GY_NET_ERR_TOKEN;
        if (host->spectator_disconnect_ms[i] &&
            (uint32_t)(now_ms - host->spectator_disconnect_ms[i]) >= GY_NET_RECONNECT_MS)
            return GY_NET_ERR_TOKEN;
        host->spectators[i].connected = 1;
        host->spectators[i].last_seq = last_seq;
        host->spectator_disconnect_ms[i] = 0;
        host->now_ms = now_ms;
        host->view.seq++;
        *color_out = GY_NET_COLOR_NONE;
        return GY_NET_OK;
    }
    return GY_NET_ERR_TOKEN;
}

int gy_net_room_leave(GyNetRoom *host, uint8_t color, uint32_t now_ms)
{
    if (!host) return GY_NET_ERR_ARG;
    host->now_ms = now_ms;
    if (color == host->view.guest_color) {
        /* 只释放席位；RESULT 仍按原节奏走完再回 WAIT，避免“客人一退，
         * 结算画面直接消失”。 */
        release_guest(host);
        host->view.seq++;
        return GY_NET_LEAVE_RELEASED;
    }
    if (color != host->view.host_color) return GY_NET_ERR_ARG;
    /* 房主主动退出：有客人时把房主转给客人（房间继续存在）；
     * 只有房主一人时关房。单房主关房后停止广播，其它板的大厅摘要
     * 会在 TTL 内消失，不会留下可点击的幽灵房间。 */
    {
        GyNetSeat *g = seat_at(host, host->view.guest_color);
        if (host->view.phase == GY_NET_PHASE_WAIT && g && g->connected) {
            uint8_t old_host = host->view.host_color;
            uint8_t new_host = host->view.guest_color;
            host->seats[new_host].role = GY_NET_ROLE_HOST;
            host->seats[old_host].role = GY_NET_ROLE_PLAYER;
            host->seats[old_host].connected = 0;
            host->seats[old_host].ready = 0;
            host->seats[old_host].player_id = 0;
            host->view.host_color = new_host;
            host->view.guest_color = old_host;
            host->view.player_count = 1;
            host->view.phase = GY_NET_PHASE_WAIT;
            host->view.phase_since_ms = now_ms;
            host->view.host_id = host->seats[new_host].player_id;
            host->view.guest_id = 0;
            sync_ready(host);
            host->view.seq++;
            host->role = GY_NET_ROLE_HOST;
            host->local_color = new_host;
            host->token = host->seats[new_host].token;
            return GY_NET_LEAVE_PROMOTED;
        }
    }
    host->view.phase = GY_NET_PHASE_CLOSED;
    host->view.player_count = 0;
    host->view.spectator_count = 0;
    host->view.host_ready = 0;
    host->view.guest_ready = 0;
    host->view.host_id = 0;
    host->view.guest_id = 0;
    host->view.seq++;
    return GY_NET_LEAVE_CLOSED;
}

int gy_net_room_leave_token(GyNetRoom *host, uint32_t token, uint32_t now_ms)
{
    int i;
    if (!host || !token) return GY_NET_ERR_ARG;
    for (i = 0; i < 2; ++i)
        if (host->seats[i].token == token) return gy_net_room_leave(host, (uint8_t)i, now_ms);
    for (i = 0; i < GY_NET_MAX_SPECTATORS; ++i) {
        if (host->spectators[i].token != token) continue;
        release_spectator(host, i);
        host->view.seq++;
        return GY_NET_LEAVE_RELEASED;
    }
    return GY_NET_ERR_TOKEN;
}

void gy_net_room_client_disconnect(GyNetRoom *client, uint32_t now_ms)
{
    if (!client) return;
    client->connected = 0;
    client->host_absent = 1;
    client->host_absent_since_ms = now_ms;
    client->now_ms = now_ms;
}

int gy_net_room_client_host_lost(GyNetRoom *client, uint32_t now_ms)
{
    if (!client || !client->host_absent) return 0;
    if ((uint32_t)(now_ms - client->host_absent_since_ms) < GY_NET_RECONNECT_MS) return 0;
    if (client->view.local_terminal) return 0;
    client->view.local_terminal = 1;
    client->now_ms = now_ms;
    return 1;
}

void gy_net_room_client_reconnect(GyNetRoom *client, uint32_t now_ms)
{
    if (!client) return;
    client->connected = 1;
    client->host_absent = 0;
    client->host_absent_since_ms = 0;
    client->view.local_terminal = 0;
    client->now_ms = now_ms;
}

void gy_net_lobby_init(GyNetLobby *lobby)
{
    if (!lobby) return;
    memset(lobby, 0, sizeof(*lobby));
}

int gy_net_lobby_update(GyNetLobby *lobby, const GyNetAdvRoom *adv, uint32_t now_ms)
{
    int i, slot = -1;
    if (!lobby || !adv || !adv->room_id) return GY_NET_ERR_ARG;
    for (i = 0; i < GY_NET_MAX_ROOMS; ++i) {
        if (lobby->rooms[i].valid && lobby->rooms[i].room_id == adv->room_id) {
            slot = i;
            break;
        }
        if (!lobby->rooms[i].valid && slot < 0) slot = i;
    }
    if (slot < 0) return GY_NET_ERR_FULL;
    lobby->rooms[slot].valid = 1;
    lobby->rooms[slot].room_id = adv->room_id;
    lobby->rooms[slot].players = adv->players;
    lobby->rooms[slot].phase = adv->phase;
    lobby->rooms[slot].spectators = adv->spectators;
    lobby->rooms[slot].max_spectators = adv->max_spectators;
    lobby->rooms[slot].last_seen_ms = now_ms;
    lobby->count = 0;
    for (i = 0; i < GY_NET_MAX_ROOMS; ++i)
        if (lobby->rooms[i].valid) lobby->count++;
    return GY_NET_OK;
}

int gy_net_lobby_tick(GyNetLobby *lobby, uint32_t now_ms)
{
    int i, removed = 0;
    if (!lobby) return 0;
    for (i = 0; i < GY_NET_MAX_ROOMS; ++i) {
        if (!lobby->rooms[i].valid) continue;
        if ((uint32_t)(now_ms - lobby->rooms[i].last_seen_ms) >= 3000u) {
            memset(&lobby->rooms[i], 0, sizeof(lobby->rooms[i]));
            removed++;
        }
    }
    if (removed) {
        int w = 0;
        for (i = 0; i < GY_NET_MAX_ROOMS; ++i)
            if (lobby->rooms[i].valid) lobby->rooms[w++] = lobby->rooms[i];
        for (; w < GY_NET_MAX_ROOMS; ++w)
            memset(&lobby->rooms[w], 0, sizeof(lobby->rooms[w]));
        lobby->count = (uint8_t)(lobby->count > removed ? lobby->count - removed : 0);
    }
    return removed;
}

int gy_net_lobby_click(const GyNetRoomSummary *room, int max_spectators)
{
    if (!room || !room->valid) return -1;
    if (room->phase == GY_NET_PHASE_RESULT || room->phase == GY_NET_PHASE_CLOSED)
        return -1;
    if (room->players < 2) return GY_NET_ROLE_PLAYER;
    if (max_spectators < 0) max_spectators = room->max_spectators;
    if (room->spectators < (uint8_t)max_spectators) return GY_NET_ROLE_SPECTATOR;
    return -1;
}

int gy_net_room_selftest(void)
{
    /* 板端 main 线程栈仅 8 KB；两个 GyNetRoom + 快照接近 3.7 KB，放静态区
     * 避免与协议自检叠加后逼近栈上限（自检只在启动时单线程运行）。 */
    static GyNetRoom host, client;
    static GyNetSnapshot snap;
    uint32_t t1 = 0, t2 = 0;
    uint8_t c1 = GY_NET_COLOR_NONE, c2 = GY_NET_COLOR_NONE;

    gy_net_room_init_host(&host, 1001, 0, 2);
    if (gy_net_room_join(&host, GY_NET_ROLE_PLAYER, 0x1234, 0, &t1, &c1) != GY_NET_OK) return -1;
    if (c1 != GY_NET_COLOR_BLUE || !t1) return -2;
    if (gy_net_room_join(&host, GY_NET_ROLE_SPECTATOR, 0x5678, 0, &t2, &c2) != GY_NET_OK) return -3;
    if (c2 != GY_NET_COLOR_NONE) return -4;
    if (gy_net_room_set_ready(&host, GY_NET_COLOR_RED, 1, 10) != GY_NET_OK) return -5;
    if (gy_net_room_set_ready(&host, GY_NET_COLOR_BLUE, 1, 20) != GY_NET_OK) return -6;
    if (host.view.phase != GY_NET_PHASE_STARTING) return -7;
    if (!gy_net_room_tick(&host, 3020)) return -8;
    if (host.view.phase != GY_NET_PHASE_PLAYING) return -9;
    gy_net_room_snapshot(&host, &snap);
    gy_net_room_init_client(&client, 1001, 0);
    if (!gy_net_room_apply_snapshot(&client, &snap)) return -10;
    if (client.view.seq != snap.seq) return -11;
    return 0;
}
