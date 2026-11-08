/*
 * geyi_net_room.h —— 联机房间的纯逻辑状态机（REQ-501~REQ-511、REQ-513）
 *
 * 设计依据：docs/02-方案设计说明.md「联机对战设计」、ADR-0007/0008/0009。
 * 本模块不依赖 LVGL、RT-Thread 或 BLE：
 *   - 房主侧：加入/席位/准备/开局/落子/快照/断线判负；
 *   - 客方侧：镜像房主状态，只接受更新 seq，不本地提交落子；
 *   - 旁观者：只读视图与预览路由；
 *   - 大厅：房间摘要的插入/更新/TTL 与点击规则。
 */
#ifndef GEYI_NET_ROOM_H
#define GEYI_NET_ROOM_H

#include "geyi_game.h"
#include "geyi_net_proto.h"

#define GY_NET_MAX_SPECTATORS 4
#define GY_NET_MAX_ROOMS      8
#define GY_NET_START_MS       3000u
#define GY_NET_RECONNECT_MS   10000u
#define GY_NET_RESULT_KEEP_MS 10000u
#define GY_NET_HEARTBEAT_MS   1000u

enum { GY_NET_ROLE_HOST = 0, GY_NET_ROLE_PLAYER = 1, GY_NET_ROLE_SPECTATOR = 2 };
enum { GY_NET_PHASE_WAIT = 0, GY_NET_PHASE_STARTING = 1,
       GY_NET_PHASE_PLAYING = 2, GY_NET_PHASE_RESULT = 3, GY_NET_PHASE_CLOSED = 4 };
enum { GY_NET_COLOR_RED = 0, GY_NET_COLOR_BLUE = 1, GY_NET_COLOR_NONE = 0xff };

/* 结算/拒绝原因。 */
enum {
    GY_NET_REASON_NORMAL = 0,
    GY_NET_REASON_RESIGN = 1,
    GY_NET_REASON_DISCONNECT = 2,
    GY_NET_REASON_LEFT = 3,
    GY_NET_REASON_HOST_LOST = 4
};

/* 离开房间的结果。 */
enum {
    GY_NET_LEAVE_RELEASED = 0,
    GY_NET_LEAVE_PROMOTED = 1,
    GY_NET_LEAVE_CLOSED = 2
};

typedef struct {
    uint8_t  role;
    uint8_t  color;
    uint8_t  ready;
    uint8_t  connected;
    uint32_t token;
    uint32_t last_seq;
} GyNetSeat;

typedef struct {
    uint32_t room_id;
    uint8_t  phase;
    uint8_t  host_color;
    uint8_t  guest_color;
    uint8_t  host_ready;
    uint8_t  guest_ready;
    uint8_t  player_count;
    uint8_t  spectator_count;
    uint8_t  max_spectators;
    uint32_t seq;
    uint32_t phase_since_ms;
    uint8_t  result;
    uint8_t  result_reason;
    uint8_t  game_valid;
    uint8_t  local_terminal;   /* 客方本地“房主断线”提示，不是权威 RESULT */
    GyGame   game;
} GyNetRoomView;

typedef struct {
    GyNetRoomView view;
    uint8_t  role;             /* 本地角色 */
    uint8_t  local_color;      /* 房主/玩家颜色；旁观为 NONE */
    uint32_t token;
    uint32_t last_seq;
    uint32_t now_ms;
    uint8_t  connected;
    uint32_t next_token;

    GyNetSeat seats[2];        /* 按颜色索引 */
    GyNetSeat spectators[GY_NET_MAX_SPECTATORS];
    uint32_t  seat_disconnect_ms[2];
    uint32_t  spectator_disconnect_ms[GY_NET_MAX_SPECTATORS];

    uint8_t   preview_valid[2];
    GyNetPreview preview[2];

    uint8_t   host_absent;
    uint32_t  host_absent_since_ms;
    GyConfig  config;           /* 联机参数：房主新开一局时使用，客户端从快照获得 */
} GyNetRoom;

void gy_net_room_init_host(GyNetRoom *room, uint32_t room_id, uint32_t now_ms,
                           uint8_t max_spectators);
void gy_net_room_init_client(GyNetRoom *room, uint32_t room_id, uint32_t now_ms);
/* 房主在开局前设置联机参数（来自 GY_MODE_PVP）；非法配置被忽略。 */
int gy_net_room_set_config(GyNetRoom *room, const GyConfig *config);
/* 等待阶段房主退出后，客方接管房间：只保留自己的席位并转成房主。 */
int gy_net_room_promote_client(GyNetRoom *client, uint8_t new_host_color, uint32_t now_ms);

/* 房主处理加入：role=PLAYER/SPECTATOR。成功返回 0，并写 token/color。 */
int gy_net_room_join(GyNetRoom *host, uint8_t role, uint32_t now_ms,
                     uint32_t *token_out, uint8_t *color_out);
int gy_net_room_set_ready(GyNetRoom *host, uint8_t color, int ready, uint32_t now_ms);
int gy_net_room_swap_seats(GyNetRoom *host);
int gy_net_room_tick(GyNetRoom *room, uint32_t now_ms);

/* 房主结算动作。成功返回 0；失败返回 GY_NET_ERR_*（TURN/ACTION/STATE）。 */
int gy_net_room_move(GyNetRoom *host, uint8_t color, int direction, int count, uint32_t now_ms);
int gy_net_room_place(GyNetRoom *host, uint8_t color, int column, uint32_t now_ms);
int gy_net_room_resign(GyNetRoom *host, uint8_t color, uint32_t now_ms);

void gy_net_room_snapshot(const GyNetRoom *host, GyNetSnapshot *out);
int gy_net_room_apply_snapshot(GyNetRoom *client, const GyNetSnapshot *snapshot);
void gy_net_room_state(const GyNetRoom *host, GyNetRoomState *out);
int gy_net_room_apply_room_state(GyNetRoom *client, const GyNetRoomState *state);
int gy_net_room_apply_result(GyNetRoom *client, const GyNetResult *result);

/* 预览：房主保存/清除；visible 决定是否转发给某角色/颜色。 */
int gy_net_room_preview(GyNetRoom *host, const GyNetPreview *preview);
int gy_net_room_preview_clear(GyNetRoom *host, uint8_t side);
int gy_net_room_preview_visible(const GyNetRoom *host, uint8_t viewer_role,
                                uint8_t viewer_color, uint8_t side);

/* 断线/重连：按 token 定位玩家或旁观者。 */
int gy_net_room_disconnect_token(GyNetRoom *host, uint32_t token, uint32_t now_ms);
int gy_net_room_rejoin(GyNetRoom *host, uint32_t token, uint32_t last_seq,
                       uint32_t now_ms, uint8_t *color_out);

/* 离开：玩家/旁观者按颜色或 token；等待期房主离开会转移或关房。 */
int gy_net_room_leave(GyNetRoom *host, uint8_t color, uint32_t now_ms);
int gy_net_room_leave_token(GyNetRoom *host, uint32_t token, uint32_t now_ms);

/* 客方本地断线：10s 后只置 local_terminal，不伪造权威 RESULT。 */
void gy_net_room_client_disconnect(GyNetRoom *client, uint32_t now_ms);
int  gy_net_room_client_host_lost(GyNetRoom *client, uint32_t now_ms);
void gy_net_room_client_reconnect(GyNetRoom *client, uint32_t now_ms);

/* 大厅摘要。 */
typedef struct {
    uint8_t  valid;
    uint32_t room_id;
    uint8_t  players;
    uint8_t  phase;
    uint8_t  spectators;
    uint8_t  max_spectators;
    uint32_t last_seen_ms;
} GyNetRoomSummary;

typedef struct {
    GyNetRoomSummary rooms[GY_NET_MAX_ROOMS];
    uint8_t count;
} GyNetLobby;

void gy_net_lobby_init(GyNetLobby *lobby);
int  gy_net_lobby_update(GyNetLobby *lobby, const GyNetAdvRoom *adv, uint32_t now_ms);
int  gy_net_lobby_tick(GyNetLobby *lobby, uint32_t now_ms);
/* 返回 GY_NET_ROLE_PLAYER / GY_NET_ROLE_SPECTATOR；观战满返回 -1。 */
int  gy_net_lobby_click(const GyNetRoomSummary *room, int max_spectators);

int gy_net_room_selftest(void);

#endif /* GEYI_NET_ROOM_H */
