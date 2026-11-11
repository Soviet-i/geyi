#ifndef GEYI_TIMER_H
#define GEYI_TIMER_H
#include <stdint.h>

/* 单局计时（REQ-604）：单调 tick 差值、处理 uint32 回绕、不支持暂停。
 * 纯逻辑，不依赖 LVGL/RT-Thread，主机可直接测试。 */
typedef struct {
    uint32_t start_ms;    /* 本次起点，仅在 running 时有效 */
    uint32_t elapsed_ms;  /* 已停止时保存的总时长 */
    int running;
} GyTimer;

void gy_timer_reset(GyTimer *timer);
void gy_timer_start(GyTimer *timer, uint32_t now_ms);
void gy_timer_stop(GyTimer *timer, uint32_t now_ms);
/* running 时返回 elapsed + (now-start)，否则返回 elapsed；无符号差值可跨回绕。 */
uint32_t gy_timer_elapsed(const GyTimer *timer, uint32_t now_ms);
int gy_timer_running(const GyTimer *timer);
/* mm:ss，超过 99:59 截断；buffer 至少 6 字节。 */
void gy_timer_format(uint32_t ms, char *out, int size);
int gy_timer_selftest(void);
#endif
