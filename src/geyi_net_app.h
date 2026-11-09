/*
 * geyi_net_app.h —— 联机应用层：把 net_room 与 BLE 传输/UI 粘起来
 *
 * 纯逻辑部分（房间状态、消息处理）不依赖 LVGL；BLE 只在 GEYI_WITH_RTTHREAD 下调用。
 * UI 只通过本头文件获取房间列表、角色、阶段、棋盘和预览，不直接碰 BLE。
 */
#ifndef GEYI_NET_APP_H
#define GEYI_NET_APP_H

#include "geyi_net_room.h"
#include "geyi_net_proto.h"

#define GY_NET_APP_MAX_ROOMS 8

/* gy_net_app_join_reason() 的返回值：UI 据此提示“房间不存在”等。 */
#define GY_NET_JOIN_REASON_NONE       0
#define GY_NET_JOIN_REASON_NOT_FOUND  1
#define GY_NET_JOIN_REASON_FULL       2
#define GY_NET_JOIN_REASON_TIMEOUT    3

typedef struct {
    uint32_t room_id;
    uint8_t  players;
    uint8_t  phase;
    uint8_t  spectators;
    uint8_t  max_spectators;
    uint8_t  addr[6];
    uint8_t  addr_type;
    int8_t   rssi;
    uint32_t seen_ms;
} GyNetAppRoom;

void gy_net_app_init(void);
void gy_net_app_tick(uint32_t now_ms);

int gy_net_app_active(void);          /* 0=无，1=房主，2=客户端/旁观 */
int gy_net_app_role(void);            /* GY_NET_ROLE_HOST/PLAYER/SPECTATOR */
int gy_net_app_phase(void);
int gy_net_app_local_color(void);
int gy_net_app_host_color(void);
int gy_net_app_guest_color(void);
int gy_net_app_local_ready(void);
int gy_net_app_host_ready(void);
int gy_net_app_guest_ready(void);
int gy_net_app_spectator_count(void);
int gy_net_app_player_count(void);
int gy_net_app_seq(void);
int gy_net_app_local_id(void);
int gy_net_app_seat_occupied(int color);
int gy_net_app_seat_ready(int color);
int gy_net_app_seat_id(int color);
int gy_net_app_connected(void);
int gy_net_app_local_terminal(void);
int gy_net_app_join_rejected(void);
int gy_net_app_join_reason(void);   /* GY_NET_JOIN_REASON_* */
int gy_net_app_spectator_lost(void);
int gy_net_app_result(void);
int gy_net_app_local_turn(void);
int gy_net_app_is_spectator(void);
int gy_net_app_pending_move(void);
const GyGame *gy_net_app_game(void);

/* 大厅 */
int gy_net_app_lobby_start(void);
void gy_net_app_lobby_stop(void);
int gy_net_app_room_count(void);
const GyNetAppRoom *gy_net_app_room_at(int index);

/* 房间操作 */
int gy_net_app_create_room(uint32_t room_id);
int gy_net_app_join_index(int index);
/* 测试/旁观入口：role=-1 按大厅规则；GY_NET_ROLE_SPECTATOR 强制旁观。 */
int gy_net_app_join_index_role(int index, int role);
void gy_net_app_leave(void);
int gy_net_app_set_ready(int ready);
int gy_net_app_swap(void);
int gy_net_app_preview(int direction, int count);
int gy_net_app_confirm(void);
int gy_net_app_cancel(void);
int gy_net_app_place(int column);
int gy_net_app_resign(void);

/* 预览可见性：UI 用 side=红/蓝 取预览；对手端由 visible 返回 0。 */
int gy_net_app_preview_visible(int side);
const GyNetPreview *gy_net_app_preview_at(int side);

int gy_net_app_selftest(void);

#endif /* GEYI_NET_APP_H */
