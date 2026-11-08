/*
 * geyi_net_link.h —— 联机传输层的可测试骨架（REQ-513）
 *
 * 当前只实现“内存管道”传输，用于 PC 双实例测试：
 *   client --client_to_host--> host
 *   host   --host_to_client--> client
 * 真实 BLE 广播/扫描/连接/收发包将在后续实现，必须复用同一套 GyNetMsg，
 * 不能让 net_room 直接依赖 BLE 头文件。
 */
#ifndef GEYI_NET_LINK_H
#define GEYI_NET_LINK_H

#include "geyi_net_proto.h"

/*
 * 该管道只用于主机双实例测试和启动自检，不承载真实 BLE 数据。
 * 板端 main 线程只有 8 KB 栈，自检会把 GyNetMemoryLink 放静态区；
 * 容量取 4 即可覆盖满/空/断开语义，同时避免 16 条消息（约 25 KB）
 * 长期占用 BSS，给 BLE 协议栈留出堆空间。
 */
#define GY_NET_PIPE_CAP 4

typedef struct {
    GyNetMsg msg[GY_NET_PIPE_CAP];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} GyNetPipe;

void gy_net_pipe_init(GyNetPipe *pipe);
int  gy_net_pipe_push(GyNetPipe *pipe, const GyNetMsg *msg);
int  gy_net_pipe_pop(GyNetPipe *pipe, GyNetMsg *msg);
int  gy_net_pipe_count(const GyNetPipe *pipe);

typedef struct {
    GyNetPipe client_to_host;
    GyNetPipe host_to_client;
    uint8_t connected;
} GyNetMemoryLink;

void gy_net_memory_link_init(GyNetMemoryLink *link);
void gy_net_memory_link_connect(GyNetMemoryLink *link, int connected);
int  gy_net_memory_link_send_client(GyNetMemoryLink *link, const GyNetMsg *msg);
int  gy_net_memory_link_send_host(GyNetMemoryLink *link, const GyNetMsg *msg);
int  gy_net_memory_link_recv_host(GyNetMemoryLink *link, GyNetMsg *msg);
int  gy_net_memory_link_recv_client(GyNetMemoryLink *link, GyNetMsg *msg);

int gy_net_link_selftest(void);

#endif /* GEYI_NET_LINK_H */
