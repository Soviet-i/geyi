/*
 * geyi_net_proto.h —— 联机消息定义与序列化（REQ-512）
 *
 * 设计依据：docs/02-方案设计说明.md「联机对战设计」第 4/6 节、ADR-0007。
 * 约束：
 *   - 消息带显式版本号与长度，明确小端字节序，不直接发送 C 结构体内存；
 *   - 解析必须做长度/类型/范围校验，截断或未知版本返回错误；
 *   - 本模块不依赖 LVGL、RT-Thread 或 BLE，可在 PC 主机测试。
 */
#ifndef GEYI_NET_PROTO_H
#define GEYI_NET_PROTO_H

#include <stdint.h>
#include "geyi_game.h"

/* v2：JOIN_REQ/JOIN_ACK/ROOM_STATE 增加 16-bit 玩家唯一 ID，
 * 用于房间页显示真实玩家名而不是对端不可知的“玩家/对手”。 */
#define GY_NET_VERSION       2
#define GY_NET_MAGIC0        'G'
#define GY_NET_MAGIC1        'Y'
#define GY_NET_HEADER_SIZE   12
#define GY_NET_MAX_PAYLOAD   768
#define GY_NET_MAX_PACKET    (GY_NET_HEADER_SIZE + GY_NET_MAX_PAYLOAD)

/* 错误码：解析/打包都必须给出具体原因。 */
#define GY_NET_OK             0
#define GY_NET_ERR_ARG      (-1)
#define GY_NET_ERR_VERSION  (-2)
#define GY_NET_ERR_LEN      (-3)
#define GY_NET_ERR_TYPE     (-4)
#define GY_NET_ERR_RANGE    (-5)
#define GY_NET_ERR_STATE    (-6)
#define GY_NET_ERR_FULL     (-7)
#define GY_NET_ERR_TURN     (-8)
#define GY_NET_ERR_ACTION   (-9)
#define GY_NET_ERR_TOKEN    (-10)

enum {
    GY_NET_ADV_ROOM = 1,
    GY_NET_JOIN_REQ,
    GY_NET_JOIN_ACK,
    GY_NET_JOIN_REJECT,
    GY_NET_ROOM_STATE,
    GY_NET_READY,
    GY_NET_SET_SEAT,
    GY_NET_LEAVE,
    GY_NET_START,
    GY_NET_MOVE_REQ,
    GY_NET_SNAPSHOT,
    GY_NET_RESULT,
    GY_NET_PREVIEW,
    GY_NET_PREVIEW_CLEAR,
    GY_NET_HEARTBEAT,
    GY_NET_PING,
    GY_NET_REJOIN,
    GY_NET_RESUME,
    GY_NET_REJECT,
    GY_NET_TYPE_COUNT
};

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint8_t  flags;
    uint8_t  reserved;
    uint32_t seq;
    uint16_t len;
    uint8_t  payload[GY_NET_MAX_PAYLOAD];
} GyNetMsg;

/* 功能级负载。字段与 docs/02 的消息表一一对应；颜色 0=红、1=蓝、0xff=无。 */
typedef struct {
    uint32_t room_id;
    uint8_t  players;
    uint8_t  phase;
    uint8_t  spectators;
    uint8_t  max_spectators;
    uint8_t  flags;          /* bit0=有玩家席位处于 10s 重连保留 */
} GyNetAdvRoom;

typedef struct {
    uint8_t  role;          /* 0=玩家，1=旁观，2=按房主当前空位自动选择 */
    uint16_t player_id;     /* 本机唯一 ID（芯片 UID 派生，见 gy_net_ble_local_id） */
    uint32_t room_id;
} GyNetJoinReq;

typedef struct {
    uint8_t  color;         /* 玩家颜色；旁观为 0xff */
    uint8_t  seat;          /* 玩家 0/1；旁观为索引 0..K-1 */
    uint16_t player_id;     /* 自己的唯一 ID，便于 ACK 丢包后校准 */
    uint32_t token;
    uint32_t room_id;
    uint32_t seq;
} GyNetJoinAck;

typedef struct {
    uint8_t  reason;
} GyNetJoinReject;

typedef struct {
    uint8_t  phase;
    uint8_t  host_color;
    uint8_t  guest_color;
    uint8_t  host_ready;
    uint8_t  guest_ready;
    uint8_t  player_count;
    uint8_t  spectator_count;
    uint16_t host_id;       /* host_color 座位的玩家 ID；无客人为 0 */
    uint16_t guest_id;      /* guest_color 座位的玩家 ID；空位为 0 */
    uint32_t room_id;
    uint32_t seq;
} GyNetRoomState;

typedef struct {
    uint8_t  ready;
} GyNetReady;

typedef struct {
    uint8_t  color;
} GyNetSetSeat;

typedef struct {
    uint8_t  reason;
} GyNetLeave;

typedef struct {
    uint8_t  host_color;
    uint8_t  guest_color;
    uint32_t seed;
    uint32_t seq;
} GyNetStart;

typedef struct {
    uint8_t  side;          /* 0=红，1=蓝 */
    uint8_t  kind;          /* 0=移动，1=放兵 */
    uint8_t  direction;     /* 移动方向 0..3 */
    uint8_t  count;         /* 移动数量 */
    uint8_t  column;        /* 放兵列 */
    uint32_t seq;
} GyNetMoveReq;

typedef struct {
    uint32_t seq;
    GyGame   game;
} GyNetSnapshot;

typedef struct {
    uint8_t  outcome;       /* GyGame outcome */
    uint8_t  reason;        /* 认输/断线/正常结束等 */
    uint32_t seq;
} GyNetResult;

typedef struct {
    uint8_t  side;
    uint8_t  direction;
    uint8_t  count;
    uint8_t  column;
    uint16_t indicator;     /* 指示点/受影响格摘要，具体编码由 UI 约定 */
    uint32_t seq;
} GyNetPreview;

typedef struct {
    uint8_t  side;
} GyNetPreviewClear;

typedef struct {
    uint32_t token;
    uint32_t last_seq;
} GyNetRejoin;

typedef struct {
    uint8_t  reason;
    uint32_t seq;
} GyNetReject;

void gy_net_msg_init(GyNetMsg *m, uint8_t type, uint32_t seq);

/* 通用信封编解码：返回写入/读取字节数，负数表示错误。 */
int gy_net_proto_encode(const GyNetMsg *m, uint8_t *out, int cap);
int gy_net_proto_decode(GyNetMsg *out, const uint8_t *in, int len);

/* 各消息的负载打包/解包。返回 GY_NET_OK 或错误码。 */
int gy_net_pack_adv_room(GyNetMsg *m, const GyNetAdvRoom *v);
int gy_net_unpack_adv_room(const GyNetMsg *m, GyNetAdvRoom *v);
int gy_net_pack_join_req(GyNetMsg *m, const GyNetJoinReq *v);
int gy_net_unpack_join_req(const GyNetMsg *m, GyNetJoinReq *v);
int gy_net_pack_join_ack(GyNetMsg *m, const GyNetJoinAck *v);
int gy_net_unpack_join_ack(const GyNetMsg *m, GyNetJoinAck *v);
int gy_net_pack_join_reject(GyNetMsg *m, const GyNetJoinReject *v);
int gy_net_unpack_join_reject(const GyNetMsg *m, GyNetJoinReject *v);
int gy_net_pack_room_state(GyNetMsg *m, const GyNetRoomState *v);
int gy_net_unpack_room_state(const GyNetMsg *m, GyNetRoomState *v);
int gy_net_pack_ready(GyNetMsg *m, const GyNetReady *v);
int gy_net_unpack_ready(const GyNetMsg *m, GyNetReady *v);
int gy_net_pack_set_seat(GyNetMsg *m, const GyNetSetSeat *v);
int gy_net_unpack_set_seat(const GyNetMsg *m, GyNetSetSeat *v);
int gy_net_pack_leave(GyNetMsg *m, const GyNetLeave *v);
int gy_net_unpack_leave(const GyNetMsg *m, GyNetLeave *v);
int gy_net_pack_start(GyNetMsg *m, const GyNetStart *v);
int gy_net_unpack_start(const GyNetMsg *m, GyNetStart *v);
int gy_net_pack_move_req(GyNetMsg *m, const GyNetMoveReq *v);
int gy_net_unpack_move_req(const GyNetMsg *m, GyNetMoveReq *v);
int gy_net_pack_snapshot(GyNetMsg *m, const GyNetSnapshot *v);
int gy_net_unpack_snapshot(const GyNetMsg *m, GyNetSnapshot *v);
int gy_net_pack_result(GyNetMsg *m, const GyNetResult *v);
int gy_net_unpack_result(const GyNetMsg *m, GyNetResult *v);
int gy_net_pack_preview(GyNetMsg *m, const GyNetPreview *v);
int gy_net_unpack_preview(const GyNetMsg *m, GyNetPreview *v);
int gy_net_pack_preview_clear(GyNetMsg *m, const GyNetPreviewClear *v);
int gy_net_unpack_preview_clear(const GyNetMsg *m, GyNetPreviewClear *v);
int gy_net_pack_rejoin(GyNetMsg *m, const GyNetRejoin *v);
int gy_net_unpack_rejoin(const GyNetMsg *m, GyNetRejoin *v);
int gy_net_pack_reject(GyNetMsg *m, const GyNetReject *v);
int gy_net_unpack_reject(const GyNetMsg *m, GyNetReject *v);

/* 主机/板端自检：返回 0 表示通过。 */
int gy_net_proto_selftest(void);

#endif /* GEYI_NET_PROTO_H */
