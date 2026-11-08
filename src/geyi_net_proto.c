/*
 * geyi_net_proto.c —— 联机消息序列化实现（REQ-512）
 *
 * 所有多字节字段按小端写入/读取；解析先校验 magic/版本/类型/长度，
 * 再做字段范围检查。快照复用 GyGame 的字段，但不直接 memcpy 结构体。
 */
#include "geyi_net_proto.h"
#include <string.h>

#define SNAPSHOT_PAYLOAD_LEN (7 * 2 + 2 + 2 * 4 + 3 * 4 + 4 + GY_MAX * GY_MAX * 8)

typedef struct {
    const uint8_t *p;
    int len;
    int pos;
    int err;
} Reader;

static int type_valid(uint8_t type)
{
    return type >= GY_NET_ADV_ROOM && type < GY_NET_TYPE_COUNT;
}

static void put8(uint8_t *p, int *pos, uint8_t v)
{
    p[(*pos)++] = v;
}
static void put16(uint8_t *p, int *pos, uint16_t v)
{
    p[(*pos)++] = (uint8_t)(v & 0xffu);
    p[(*pos)++] = (uint8_t)((v >> 8) & 0xffu);
}
static void put32(uint8_t *p, int *pos, uint32_t v)
{
    p[(*pos)++] = (uint8_t)(v & 0xffu);
    p[(*pos)++] = (uint8_t)((v >> 8) & 0xffu);
    p[(*pos)++] = (uint8_t)((v >> 16) & 0xffu);
    p[(*pos)++] = (uint8_t)((v >> 24) & 0xffu);
}

static uint8_t get8(Reader *r)
{
    if (r->pos >= r->len) { r->err = GY_NET_ERR_LEN; return 0; }
    return r->p[r->pos++];
}
static uint16_t get16(Reader *r)
{
    uint16_t a = get8(r);
    uint16_t b = get8(r);
    return (uint16_t)(a | (b << 8));
}
static uint32_t get32(Reader *r)
{
    uint32_t a = get8(r), b = get8(r), c = get8(r), d = get8(r);
    return a | (b << 8) | (c << 16) | (d << 24);
}

void gy_net_msg_init(GyNetMsg *m, uint8_t type, uint32_t seq)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->version = GY_NET_VERSION;
    m->type = type;
    m->seq = seq;
    m->len = 0;
}

int gy_net_proto_encode(const GyNetMsg *m, uint8_t *out, int cap)
{
    int pos = 0;
    if (!m || !out || cap < 0) return GY_NET_ERR_ARG;
    if (m->version != GY_NET_VERSION) return GY_NET_ERR_VERSION;
    if (!type_valid(m->type)) return GY_NET_ERR_TYPE;
    if (m->len > GY_NET_MAX_PAYLOAD) return GY_NET_ERR_LEN;
    if (cap < GY_NET_HEADER_SIZE + (int)m->len) return GY_NET_ERR_LEN;
    out[pos++] = GY_NET_MAGIC0;
    out[pos++] = GY_NET_MAGIC1;
    out[pos++] = GY_NET_VERSION;
    out[pos++] = m->type;
    out[pos++] = m->flags;
    out[pos++] = m->reserved;
    put32(out, &pos, m->seq);
    put16(out, &pos, m->len);
    if (m->len) memcpy(out + pos, m->payload, m->len);
    pos += (int)m->len;
    return pos;
}

int gy_net_proto_decode(GyNetMsg *out, const uint8_t *in, int len)
{
    uint16_t payload_len;
    if (!out || !in || len < 0) return GY_NET_ERR_ARG;
    if (len < GY_NET_HEADER_SIZE) return GY_NET_ERR_LEN;
    if (in[0] != GY_NET_MAGIC0 || in[1] != GY_NET_MAGIC1) return GY_NET_ERR_ARG;
    if (in[2] != GY_NET_VERSION) return GY_NET_ERR_VERSION;
    if (!type_valid(in[3])) return GY_NET_ERR_TYPE;
    payload_len = (uint16_t)(in[10] | ((uint16_t)in[11] << 8));
    if (payload_len > GY_NET_MAX_PAYLOAD) return GY_NET_ERR_LEN;
    if (len != GY_NET_HEADER_SIZE + (int)payload_len) return GY_NET_ERR_LEN;
    memset(out, 0, sizeof(*out));
    out->version = in[2];
    out->type = in[3];
    out->flags = in[4];
    out->reserved = in[5];
    out->seq = (uint32_t)in[6] | ((uint32_t)in[7] << 8) |
               ((uint32_t)in[8] << 16) | ((uint32_t)in[9] << 24);
    out->len = payload_len;
    if (payload_len) memcpy(out->payload, in + GY_NET_HEADER_SIZE, payload_len);
    return GY_NET_OK;
}

#define PACK_BEGIN(m, type_, seq_) do { \
    if (!(m)) return GY_NET_ERR_ARG; \
    gy_net_msg_init((m), (type_), (seq_)); \
    pos = 0; \
} while (0)
#define PACK_END(m) do { (m)->len = (uint16_t)pos; return GY_NET_OK; } while (0)

static int unpack_begin(const GyNetMsg *m, uint8_t type, Reader *r, int expected)
{
    if (!m || !r) return GY_NET_ERR_ARG;
    if (m->type != type) return GY_NET_ERR_TYPE;
    if ((int)m->len != expected) return GY_NET_ERR_LEN;
    r->p = m->payload;
    r->len = (int)m->len;
    r->pos = 0;
    r->err = 0;
    return GY_NET_OK;
}
static int unpack_end(const Reader *r)
{
    if (r->err) return r->err;
    return GY_NET_OK;
}

int gy_net_pack_adv_room(GyNetMsg *m, const GyNetAdvRoom *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_ADV_ROOM, 0);
    put32(m->payload, &pos, v->room_id);
    put8(m->payload, &pos, v->players);
    put8(m->payload, &pos, v->phase);
    put8(m->payload, &pos, v->spectators);
    put8(m->payload, &pos, v->max_spectators);
    PACK_END(m);
}
int gy_net_unpack_adv_room(const GyNetMsg *m, GyNetAdvRoom *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_ADV_ROOM, &r, 8); if (rc) return rc;
    v->room_id = get32(&r);
    v->players = get8(&r);
    v->phase = get8(&r);
    v->spectators = get8(&r);
    v->max_spectators = get8(&r);
    if (unpack_end(&r)) return r.err;
    if (v->players > 2 || v->phase > 4u ||
        v->spectators > 8 || v->max_spectators > 8) return GY_NET_ERR_RANGE;
    return GY_NET_OK;
}

int gy_net_pack_join_req(GyNetMsg *m, const GyNetJoinReq *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_JOIN_REQ, 0);
    put8(m->payload, &pos, v->role);
    put32(m->payload, &pos, v->room_id);
    PACK_END(m);
}
int gy_net_unpack_join_req(const GyNetMsg *m, GyNetJoinReq *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_JOIN_REQ, &r, 5); if (rc) return rc;
    v->role = get8(&r);
    v->room_id = get32(&r);
    if (unpack_end(&r)) return r.err;
    if (v->role > 1) return GY_NET_ERR_RANGE;
    return GY_NET_OK;
}

int gy_net_pack_join_ack(GyNetMsg *m, const GyNetJoinAck *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_JOIN_ACK, v->seq);
    put8(m->payload, &pos, v->color);
    put8(m->payload, &pos, v->seat);
    put32(m->payload, &pos, v->token);
    put32(m->payload, &pos, v->room_id);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_join_ack(const GyNetMsg *m, GyNetJoinAck *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_JOIN_ACK, &r, 14); if (rc) return rc;
    v->color = get8(&r);
    v->seat = get8(&r);
    v->token = get32(&r);
    v->room_id = get32(&r);
    v->seq = get32(&r);
    return unpack_end(&r);
}

int gy_net_pack_join_reject(GyNetMsg *m, const GyNetJoinReject *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_JOIN_REJECT, 0);
    put8(m->payload, &pos, v->reason);
    PACK_END(m);
}
int gy_net_unpack_join_reject(const GyNetMsg *m, GyNetJoinReject *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_JOIN_REJECT, &r, 1); if (rc) return rc;
    v->reason = get8(&r);
    return unpack_end(&r);
}

int gy_net_pack_room_state(GyNetMsg *m, const GyNetRoomState *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_ROOM_STATE, v->seq);
    put8(m->payload, &pos, v->phase);
    put8(m->payload, &pos, v->host_color);
    put8(m->payload, &pos, v->guest_color);
    put8(m->payload, &pos, v->host_ready);
    put8(m->payload, &pos, v->guest_ready);
    put8(m->payload, &pos, v->player_count);
    put8(m->payload, &pos, v->spectator_count);
    put32(m->payload, &pos, v->room_id);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_room_state(const GyNetMsg *m, GyNetRoomState *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_ROOM_STATE, &r, 15); if (rc) return rc;
    v->phase = get8(&r);
    v->host_color = get8(&r);
    v->guest_color = get8(&r);
    v->host_ready = get8(&r);
    v->guest_ready = get8(&r);
    v->player_count = get8(&r);
    v->spectator_count = get8(&r);
    v->room_id = get32(&r);
    v->seq = get32(&r);
    if (unpack_end(&r)) return r.err;
    if (v->phase > 4u ||
        v->host_color > 1 || v->guest_color > 1 ||
        v->host_color == v->guest_color ||
        v->player_count > 2 || v->spectator_count > 8) return GY_NET_ERR_RANGE;
    return GY_NET_OK;
}

int gy_net_pack_ready(GyNetMsg *m, const GyNetReady *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_READY, 0);
    put8(m->payload, &pos, v->ready);
    PACK_END(m);
}
int gy_net_unpack_ready(const GyNetMsg *m, GyNetReady *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_READY, &r, 1); if (rc) return rc;
    v->ready = get8(&r);
    return unpack_end(&r);
}

int gy_net_pack_set_seat(GyNetMsg *m, const GyNetSetSeat *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_SET_SEAT, 0);
    put8(m->payload, &pos, v->color);
    PACK_END(m);
}
int gy_net_unpack_set_seat(const GyNetMsg *m, GyNetSetSeat *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_SET_SEAT, &r, 1); if (rc) return rc;
    v->color = get8(&r);
    return unpack_end(&r);
}

int gy_net_pack_leave(GyNetMsg *m, const GyNetLeave *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_LEAVE, 0);
    put8(m->payload, &pos, v->reason);
    PACK_END(m);
}
int gy_net_unpack_leave(const GyNetMsg *m, GyNetLeave *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_LEAVE, &r, 1); if (rc) return rc;
    v->reason = get8(&r);
    return unpack_end(&r);
}

int gy_net_pack_start(GyNetMsg *m, const GyNetStart *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_START, v->seq);
    put8(m->payload, &pos, v->host_color);
    put8(m->payload, &pos, v->guest_color);
    put32(m->payload, &pos, v->seed);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_start(const GyNetMsg *m, GyNetStart *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_START, &r, 10); if (rc) return rc;
    v->host_color = get8(&r);
    v->guest_color = get8(&r);
    v->seed = get32(&r);
    v->seq = get32(&r);
    return unpack_end(&r);
}

int gy_net_pack_move_req(GyNetMsg *m, const GyNetMoveReq *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_MOVE_REQ, v->seq);
    put8(m->payload, &pos, v->side);
    put8(m->payload, &pos, v->kind);
    put8(m->payload, &pos, v->direction);
    put8(m->payload, &pos, v->count);
    put8(m->payload, &pos, v->column);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_move_req(const GyNetMsg *m, GyNetMoveReq *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_MOVE_REQ, &r, 9); if (rc) return rc;
    v->side = get8(&r);
    v->kind = get8(&r);
    v->direction = get8(&r);
    v->count = get8(&r);
    v->column = get8(&r);
    v->seq = get32(&r);
    if (unpack_end(&r)) return r.err;
    if (v->side > 1 || v->kind > 1 || v->direction > 3) return GY_NET_ERR_RANGE;
    return GY_NET_OK;
}

static void put_cell(uint8_t *p, int *pos, const GyCell *c)
{
    put8(p, pos, (uint8_t)c->kind);
    put8(p, pos, (uint8_t)c->owner);
    put16(p, pos, (uint16_t)c->hp);
    put32(p, pos, c->value);
}
static int get_cell(Reader *r, GyCell *c)
{
    c->kind = get8(r);
    c->owner = get8(r);
    c->hp = get16(r);
    c->value = get32(r);
    if (r->err) return r->err;
    if (c->kind < GY_EMPTY || c->kind > GY_NUMBER) return GY_NET_ERR_RANGE;
    if (c->owner < 0 || c->owner > 1) return GY_NET_ERR_RANGE;
    if (c->hp < 0 || c->hp > 10000) return GY_NET_ERR_RANGE;
    return GY_NET_OK;
}

int gy_net_pack_snapshot(GyNetMsg *m, const GyNetSnapshot *v)
{
    int pos, x, y;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_SNAPSHOT, v->seq);
    put16(m->payload, &pos, (uint16_t)v->game.config.size);
    put16(m->payload, &pos, (uint16_t)v->game.config.hp);
    put16(m->payload, &pos, (uint16_t)v->game.config.attack);
    put16(m->payload, &pos, (uint16_t)v->game.config.cost);
    put16(m->payload, &pos, (uint16_t)v->game.config.merge_reward);
    put16(m->payload, &pos, (uint16_t)v->game.config.kill_reward);
    put16(m->payload, &pos, (uint16_t)v->game.config.spawn_interval);
    put8(m->payload, &pos, (uint8_t)v->game.turn);
    put8(m->payload, &pos, (uint8_t)v->game.outcome);
    put32(m->payload, &pos, (uint32_t)v->game.coins[0]);
    put32(m->payload, &pos, (uint32_t)v->game.coins[1]);
    put32(m->payload, &pos, v->game.revision);
    put32(m->payload, &pos, v->game.moves);
    put32(m->payload, &pos, v->game.rng);
    for (y = 0; y < GY_MAX; ++y)
        for (x = 0; x < GY_MAX; ++x)
            put_cell(m->payload, &pos, &v->game.cells[y][x]);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_snapshot(const GyNetMsg *m, GyNetSnapshot *v)
{
    Reader r;
    int rc, x, y;
    uint16_t size;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_SNAPSHOT, &r, SNAPSHOT_PAYLOAD_LEN); if (rc) return rc;
    memset(v, 0, sizeof(*v));
    size = get16(&r);
    if (size < 4 || size > GY_MAX) return GY_NET_ERR_RANGE;
    v->game.config.size = (int)size;
    v->game.config.hp = get16(&r);
    v->game.config.attack = get16(&r);
    v->game.config.cost = get16(&r);
    v->game.config.merge_reward = get16(&r);
    v->game.config.kill_reward = get16(&r);
    v->game.config.spawn_interval = get16(&r);
    v->game.turn = get8(&r);
    v->game.outcome = get8(&r);
    v->game.coins[0] = (int)get32(&r);
    v->game.coins[1] = (int)get32(&r);
    v->game.revision = get32(&r);
    v->game.moves = get32(&r);
    v->game.rng = get32(&r);
    for (y = 0; y < GY_MAX; ++y) {
        for (x = 0; x < GY_MAX; ++x) {
            rc = get_cell(&r, &v->game.cells[y][x]);
            if (rc) return rc;
        }
    }
    v->seq = get32(&r);
    if (v->game.config.hp < 1 || v->game.config.attack < 1 || v->game.config.cost < 1)
        return GY_NET_ERR_RANGE;
    return unpack_end(&r);
}

int gy_net_pack_result(GyNetMsg *m, const GyNetResult *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_RESULT, v->seq);
    put8(m->payload, &pos, v->outcome);
    put8(m->payload, &pos, v->reason);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_result(const GyNetMsg *m, GyNetResult *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_RESULT, &r, 6); if (rc) return rc;
    v->outcome = get8(&r);
    v->reason = get8(&r);
    v->seq = get32(&r);
    if (unpack_end(&r)) return r.err;
    if (v->outcome > GY_DRAW) return GY_NET_ERR_RANGE;
    return GY_NET_OK;
}

int gy_net_pack_preview(GyNetMsg *m, const GyNetPreview *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_PREVIEW, v->seq);
    put8(m->payload, &pos, v->side);
    put8(m->payload, &pos, v->direction);
    put8(m->payload, &pos, v->count);
    put8(m->payload, &pos, v->column);
    put16(m->payload, &pos, v->indicator);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_preview(const GyNetMsg *m, GyNetPreview *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_PREVIEW, &r, 10); if (rc) return rc;
    v->side = get8(&r);
    v->direction = get8(&r);
    v->count = get8(&r);
    v->column = get8(&r);
    v->indicator = get16(&r);
    v->seq = get32(&r);
    if (unpack_end(&r)) return r.err;
    if (v->side > 1 || v->direction > 3) return GY_NET_ERR_RANGE;
    return GY_NET_OK;
}

int gy_net_pack_preview_clear(GyNetMsg *m, const GyNetPreviewClear *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_PREVIEW_CLEAR, 0);
    put8(m->payload, &pos, v->side);
    PACK_END(m);
}
int gy_net_unpack_preview_clear(const GyNetMsg *m, GyNetPreviewClear *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_PREVIEW_CLEAR, &r, 1); if (rc) return rc;
    v->side = get8(&r);
    return unpack_end(&r);
}

int gy_net_pack_rejoin(GyNetMsg *m, const GyNetRejoin *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_REJOIN, v->last_seq);
    put32(m->payload, &pos, v->token);
    put32(m->payload, &pos, v->last_seq);
    PACK_END(m);
}
int gy_net_unpack_rejoin(const GyNetMsg *m, GyNetRejoin *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_REJOIN, &r, 8); if (rc) return rc;
    v->token = get32(&r);
    v->last_seq = get32(&r);
    return unpack_end(&r);
}

int gy_net_pack_reject(GyNetMsg *m, const GyNetReject *v)
{
    int pos;
    if (!v) return GY_NET_ERR_ARG;
    PACK_BEGIN(m, GY_NET_REJECT, v->seq);
    put8(m->payload, &pos, v->reason);
    put32(m->payload, &pos, v->seq);
    PACK_END(m);
}
int gy_net_unpack_reject(const GyNetMsg *m, GyNetReject *v)
{
    Reader r;
    int rc;
    if (!v) return GY_NET_ERR_ARG;
    rc = unpack_begin(m, GY_NET_REJECT, &r, 5); if (rc) return rc;
    v->reason = get8(&r);
    v->seq = get32(&r);
    return unpack_end(&r);
}

int gy_net_proto_selftest(void)
{
    /* 快照 1 KB + 两个 GyNetMsg 1.5 KB + 发送缓冲 780 B，合计约 5.4 KB，
     * 在 8 KB 的 main 线程栈上余量过小；自检改放静态区，避免板端 STKOF。 */
    static GyGame g;
    static GyNetSnapshot s, out;
    static GyNetMsg in, msg;
    static uint8_t buf[GY_NET_MAX_PACKET];
    int n;

    if (!gy_init(&g, gy_default_config(), 7)) return -1;
    memset(&s, 0, sizeof(s));
    s.seq = 42;
    s.game = g;
    if (gy_net_pack_snapshot(&in, &s) != GY_NET_OK) return -2;
    n = gy_net_proto_encode(&in, buf, (int)sizeof(buf));
    if (n <= 0) return -3;
    if (gy_net_proto_decode(&msg, buf, n) != GY_NET_OK) return -4;
    if (gy_net_unpack_snapshot(&msg, &out) != GY_NET_OK) return -5;
    if (out.seq != s.seq || out.game.config.size != s.game.config.size ||
        out.game.turn != s.game.turn || out.game.rng != s.game.rng) return -6;
    if (memcmp(out.game.cells, s.game.cells, sizeof(s.game.cells)) != 0) return -7;

    if (gy_net_proto_decode(&msg, buf, n - 1) != GY_NET_ERR_LEN) return -8;
    buf[2] = (uint8_t)(GY_NET_VERSION + 1);
    if (gy_net_proto_decode(&msg, buf, n) != GY_NET_ERR_VERSION) return -9;
    return 0;
}
