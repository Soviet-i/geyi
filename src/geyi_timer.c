#include "geyi_timer.h"
#include <stdio.h>

void gy_timer_reset(GyTimer *timer)
{
    if (!timer) return;
    timer->start_ms = 0;
    timer->elapsed_ms = 0;
    timer->running = 0;
}

void gy_timer_start(GyTimer *timer, uint32_t now_ms)
{
    if (!timer) return;
    timer->start_ms = now_ms;
    timer->elapsed_ms = 0;
    timer->running = 1;
}

void gy_timer_stop(GyTimer *timer, uint32_t now_ms)
{
    if (!timer || !timer->running) return;
    timer->elapsed_ms += (uint32_t)(now_ms - timer->start_ms);
    timer->running = 0;
}

uint32_t gy_timer_elapsed(const GyTimer *timer, uint32_t now_ms)
{
    if (!timer) return 0;
    if (!timer->running) return timer->elapsed_ms;
    return timer->elapsed_ms + (uint32_t)(now_ms - timer->start_ms);
}

int gy_timer_running(const GyTimer *timer)
{
    return timer && timer->running;
}

void gy_timer_format(uint32_t ms, char *out, int size)
{
    uint32_t minutes, seconds;
    if (!out || size <= 0) return;
    minutes = ms / 60000u;
    seconds = (ms / 1000u) % 60u;
    if (minutes > 99u) { minutes = 99u; seconds = 59u; }
    snprintf(out, (size_t)size, "%02u:%02u", (unsigned)minutes, (unsigned)seconds);
}

int gy_timer_selftest(void)
{
    GyTimer timer;
    char text[8];

    gy_timer_reset(&timer);
    if (gy_timer_running(&timer) || gy_timer_elapsed(&timer, 500u) != 0u) return 0;

    gy_timer_start(&timer, 1000u);
    if (!gy_timer_running(&timer)) return 0;
    if (gy_timer_elapsed(&timer, 1000u) != 0u) return 0;
    if (gy_timer_elapsed(&timer, 115000u) != 114000u) return 0;

    /* 跨 uint32 回绕：起点接近上限，now 已经翻转。 */
    gy_timer_start(&timer, 0xFFFFFF00u);
    if (gy_timer_elapsed(&timer, 0x00000100u) != 512u) return 0;

    gy_timer_start(&timer, 500u);
    gy_timer_stop(&timer, 2500u);
    if (gy_timer_running(&timer) || gy_timer_elapsed(&timer, 9999u) != 2000u) return 0;
    gy_timer_stop(&timer, 9999u);   /* 重复停止不改变结果 */
    if (gy_timer_elapsed(&timer, 9999u) != 2000u) return 0;

    gy_timer_format(0u, text, (int)sizeof(text));
    if (text[0] != '0' || text[1] != '0' || text[2] != ':') return 0;
    gy_timer_format(60000u, text, (int)sizeof(text));
    if (text[0] != '0' || text[1] != '1' || text[3] != '0') return 0;
    gy_timer_format(6000000u, text, (int)sizeof(text));
    if (text[0] != '9' || text[1] != '9' || text[4] != '9') return 0;

    gy_timer_reset(&timer);
    return gy_timer_elapsed(&timer, 12345u) == 0u && !gy_timer_running(&timer);
}
