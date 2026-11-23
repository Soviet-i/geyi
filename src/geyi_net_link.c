/*
 * geyi_net_link.c —— 联机内存管道实现（REQ-513）
 *
 * 环形队列固定容量、无动态分配；满/空返回 0，不阻塞。
 * 该实现只用于主机双实例测试和 UI 联调，不代表 BLE 已实现。
 */
#include "geyi_net_link.h"
#include <string.h>

void gy_net_pipe_init(GyNetPipe *pipe)
{
    if (!pipe) return;
    memset(pipe, 0, sizeof(*pipe));
}

int gy_net_pipe_push(GyNetPipe *pipe, const GyNetMsg *msg)
{
    if (!pipe || !msg) return 0;
    if (pipe->count >= GY_NET_PIPE_CAP) return 0;
    pipe->msg[pipe->tail] = *msg;
    pipe->tail = (uint8_t)((pipe->tail + 1) % GY_NET_PIPE_CAP);
    pipe->count++;
    return 1;
}

int gy_net_pipe_pop(GyNetPipe *pipe, GyNetMsg *msg)
{
    if (!pipe || !msg || !pipe->count) return 0;
    *msg = pipe->msg[pipe->head];
    pipe->head = (uint8_t)((pipe->head + 1) % GY_NET_PIPE_CAP);
    pipe->count--;
    return 1;
}

int gy_net_pipe_count(const GyNetPipe *pipe)
{
    return pipe ? (int)pipe->count : 0;
}

void gy_net_memory_link_init(GyNetMemoryLink *link)
{
    if (!link) return;
    memset(link, 0, sizeof(*link));
    gy_net_pipe_init(&link->client_to_host);
    gy_net_pipe_init(&link->host_to_client);
    link->connected = 1;
}

void gy_net_memory_link_connect(GyNetMemoryLink *link, int connected)
{
    if (link) link->connected = connected ? 1 : 0;
}

int gy_net_memory_link_send_client(GyNetMemoryLink *link, const GyNetMsg *msg)
{
    if (!link || !link->connected) return 0;
    return gy_net_pipe_push(&link->client_to_host, msg);
}

int gy_net_memory_link_send_host(GyNetMemoryLink *link, const GyNetMsg *msg)
{
    if (!link || !link->connected) return 0;
    return gy_net_pipe_push(&link->host_to_client, msg);
}

int gy_net_memory_link_recv_host(GyNetMemoryLink *link, GyNetMsg *msg)
{
    if (!link) return 0;
    return gy_net_pipe_pop(&link->client_to_host, msg);
}

int gy_net_memory_link_recv_client(GyNetMemoryLink *link, GyNetMsg *msg)
{
    if (!link) return 0;
    return gy_net_pipe_pop(&link->host_to_client, msg);
}

int gy_net_link_selftest(void)
{
    /*
     * 板端 main 线程栈只有 8 KB（rtconfig.h），而 GyNetMemoryLink 约 25 KB。
     * 主机测试的线程栈有几 MB，所以这个栈帧在 PC 上不会暴露问题，真机会直接
     * STKOF。自检只跑一次且不与其他线程并发，放在静态区，避免占用调用栈。
     */
    static GyNetMemoryLink link;
    static GyNetMsg in, out;
    int i;

    gy_net_memory_link_init(&link);
    for (i = 0; i < GY_NET_PIPE_CAP; ++i) {
        gy_net_msg_init(&in, GY_NET_PING, (uint32_t)i);
        if (!gy_net_memory_link_send_client(&link, &in)) return -1;
    }
    gy_net_msg_init(&in, GY_NET_PING, 99);
    if (gy_net_memory_link_send_client(&link, &in)) return -2;
    if (gy_net_pipe_count(&link.client_to_host) != GY_NET_PIPE_CAP) return -3;
    for (i = 0; i < GY_NET_PIPE_CAP; ++i) {
        if (!gy_net_memory_link_recv_host(&link, &out)) return -4;
        if (out.seq != (uint32_t)i) return -5;
    }
    if (gy_net_memory_link_recv_host(&link, &out)) return -6;
    gy_net_memory_link_connect(&link, 0);
    gy_net_msg_init(&in, GY_NET_PING, 1);
    if (gy_net_memory_link_send_client(&link, &in)) return -7;
    return 0;
}
