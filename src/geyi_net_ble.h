/*
 * geyi_net_ble.h —— 联机 BLE 传输（REQ-501/508/512，实现于 RT-Thread/SDK）
 *
 * 分工：
 *   - 房主：BLE 外设 + GATT 服务端，广播房间摘要，向已连接客户端 notify；
 *   - 对手/旁观者：BLE 中央 + GATT 客户端，扫描/连接房主并写特征值。
 * 本层只负责“字节可靠收发 + 分片重组 + 广播摘要”，不碰棋局规则；
 * 消息编解码使用 geyi_net_proto，房间状态使用 geyi_net_room。
 *
 * 注意：本头文件只在 GEYI_WITH_RTTHREAD（固件）下可用；桌面 LVGL 构建不包含。
 */
#ifndef GEYI_NET_BLE_H
#define GEYI_NET_BLE_H

#include "geyi_net_proto.h"

#ifdef GEYI_WITH_RTTHREAD

#define GY_NET_BLE_MAX_CONN 4
#define GY_NET_BLE_BROADCAST_CONN 0xff

typedef struct {
    GyNetMsg msg;
    uint8_t  conn;
    /* 链路代际：同一个 conn_idx 被复用时，旧会话的迟到包必须被丢弃。 */
    uint8_t  gen;
} GyNetBleRx;

typedef struct {
    GyNetAdvRoom adv;
    uint8_t      addr[6];
    uint8_t      addr_type;
    int8_t       rssi;
} GyNetBleAdv;

/* 初始化 BLE 栈；必须在主线程/启动阶段调用一次。 */
void gy_net_ble_init(void);
/* 主线程轮询：BLE_POWER_ON_IND 之后注册 GATT（不能在事件回调里注册）。 */
void gy_net_ble_poll(void);

/* 房主：启动/更新/停止可连接广播。满员时可传 connectable=0 改为只广播摘要。 */
int  gy_net_ble_host_start(uint32_t room_id, uint8_t players, uint8_t phase,
                           uint8_t spectators, uint8_t max_spectators);
int  gy_net_ble_host_update(uint8_t players, uint8_t phase, uint8_t spectators,
                            uint8_t max_spectators, int connectable,
                            uint8_t flags);
void gy_net_ble_host_stop(void);

/* 客户端：扫描房间、连接指定地址、断开。 */
int  gy_net_ble_scan_start(void);
void gy_net_ble_scan_stop(void);
int  gy_net_ble_connect(const uint8_t addr[6], uint8_t addr_type);
void gy_net_ble_disconnect(uint8_t conn);
/* 客户端中止：取消在途连接、断开现有链路、清 remote service/搜索状态。
 * 离房、加入失败、重新进入大厅时必须调用，否则在途连接会在离开后
 * 才建立，留下一条没有应用会话的僵尸链路（房主广播被占住）。 */
void gy_net_ble_client_abort(void);
/* 客户端把 LEAVE 交给 TX 线程发送，发送完成后由 TX 线程断开；UI 不阻塞。 */
void gy_net_ble_client_leave(uint8_t conn, const GyNetMsg *msg);

/* 连接状态。 */
int      gy_net_ble_connected(void);
int      gy_net_ble_ready(void);       /* 客户端：服务发现/CCCD 就绪，可收 ACK */
int      gy_net_ble_write_ready(void); /* 客户端：RX 可写即可发 JOIN，ACK 由重发兜底 */
uint8_t  gy_net_ble_conn_count(void);
uint8_t  gy_net_ble_conn_at(uint8_t index);
uint8_t  gy_net_ble_conn_gen(uint8_t conn);   /* 0=该 conn_idx 当前不存在 */
uint8_t  gy_net_ble_client_conn(void);
uint8_t  gy_net_ble_client_gen(void);
uint16_t gy_net_ble_mtu(uint8_t conn);
/* 房主当前是否仍在发可连接广播（用于“连接后继续接受第二个人”）。 */
int      gy_net_ble_host_adv_active(void);
/* 本板 BLE 地址后 2 字节，作为 UI 的简短玩家标识（如 #FD5C）。 */
int      gy_net_ble_local_id(void);

/* 发送：conn 为具体连接号；GY_NET_BLE_BROADCAST_CONN 表示房主发给所有已连接端。 */
int gy_net_ble_send(uint8_t conn, const GyNetMsg *msg);
/* 等待 TX 队列清空（最多 timeout_ms），用于退出/断开前把 LEAVE/RESULT 发出去。 */
void gy_net_ble_flush(uint32_t timeout_ms);

/* 轮询收到的消息/广播摘要。返回 1 表示取到。 */
int gy_net_ble_recv(GyNetBleRx *out);
void gy_net_ble_rx_flush(void);   /* 离房/换房时丢弃上一房间的迟到包 */
void gy_net_ble_adv_flush(void);  /* 进入/离开大厅时清掉扫描结果，避免幽灵房 */
int gy_net_ble_adv_recv(GyNetBleAdv *out);

int gy_net_ble_selftest(void);

#endif /* GEYI_WITH_RTTHREAD */
#endif /* GEYI_NET_BLE_H */
