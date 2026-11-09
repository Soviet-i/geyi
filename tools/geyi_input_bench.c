/*
 * geyi_input_bench.c —— 倾斜输入的量化测量（对应 docs/06 的 L-03）
 *
 * 目的：把"倾斜误触发、正确率、延迟未量化"变成可复现的数字。
 *
 * 重要边界：本工具测的是**真实输入状态机**（src/geyi_input.c）在
 * 给定采样周期下的行为，是**主机仿真**，不是板端实测。
 * 板端还会叠加 IMU 采样抖动、I2C 传输与调度延迟，因此本结果只能作为
 * "状态机本身的延迟下限"，不能写成真机延迟。
 *
 * 输出：CSV 到 stdout（metric,step_ms,param,value,expected,unit）
 *
 * 用法：geyi-input-bench [--trials N] [--seed N]
 */
#include "geyi_input.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- 仿真骨架：每次喂一个样本，时间自增 step ---- */
typedef struct {
    GyInput  in;
    uint32_t now;
    int      step;         /* 采样周期 ms */
    int      avail[4];
} Sim;

static void sim_init(Sim *s, int step, int dir_pct, int angle_pct, int avail)
{
    int d;
    memset(s, 0, sizeof(*s));
    gy_input_reset(&s->in);
    gy_input_configure(&s->in, dir_pct, angle_pct);
    s->step = step;
    for (d = 0; d < 4; ++d) s->avail[d] = avail;
}

static GyInputEvent sim_feed(Sim *s, int x, int y, int key)
{
    s->now += (uint32_t)s->step;
    return gy_input_update(&s->in, x, y, key, s->now, s->avail);
}

/* 从"新输入的第一个样本"起计时，直到出现 want 事件；返回毫秒，超时返回 -1 */
static int feed_until(Sim *s, int x, int y, int key, int want, int max_ms)
{
    int waited = 0;
    while (waited <= max_ms) {
        GyInputEvent e = sim_feed(s, x, y, key);
        waited += s->step;
        if (e.events & want) return waited;
    }
    return -1;
}

/* 回中若干周期以解除 latched（输入模块要求先水平） */
static void settle_level(Sim *s, int cycles)
{
    int i;
    for (i = 0; i < cycles; ++i) sim_feed(s, 0, 0, 0);
}

/* 建立"指向 UP、数量 1"的稳定选择 */
static int establish_up(Sim *s, int mag)
{
    settle_level(s, 6);
    return feed_until(s, 0, -mag, 0, GY_INPUT_SELECT, 2000);
}

/* ---- 可复现随机数 ---- */
static uint32_t s_rng = 1;
static void seed_rng(uint32_t v) { s_rng = v ? v : 1; }
static int rnd_range(int lo, int hi)      /* 闭区间 */
{
    s_rng = s_rng * 1103515245u + 12345u;
    return lo + (int)((s_rng >> 16) % (uint32_t)(hi - lo + 1));
}

/* ---- M1：从开始倾斜到 SELECT 的延迟 ---- */
static void m_select_latency(int trials)
{
    static const int steps[] = {1, 5, 10, 20};
    static const int pcts[]  = {0, 10, 50};
    int si, pi, k;
    for (si = 0; si < (int)(sizeof steps / sizeof steps[0]); ++si) {
        for (pi = 0; pi < (int)(sizeof pcts / sizeof pcts[0]); ++pi) {
            long sum = 0;
            int ok = 0;
            for (k = 0; k < trials; ++k) {
                Sim s;
                int lat;
                sim_init(&s, steps[si], pcts[pi], 10, 2);
                settle_level(&s, 6);
                lat = feed_until(&s, 0, -2000, 0, GY_INPUT_SELECT, 2000);
                if (lat >= 0) { sum += lat; ++ok; }
            }
            printf("select_latency,%d,dir_pct=%d,%ld,>=200,ms\n",
                   steps[si], pcts[pi], ok ? sum / ok : -1);
        }
    }
}

/* ---- M2：方向切换的"最小强度比"阈值（百分比）与切换延迟 ---- */
static int switches_at(int step, int dir_pct, int ratio_pct)
{
    /* 先在 UP(2000) 上稳定，再把横轴设为比纵轴强 ratio_pct% */
    Sim s;
    int ax = 2000 + (2000 * ratio_pct) / 100;
    sim_init(&s, step, dir_pct, 10, 2);
    if (establish_up(&s, 2000) < 0) return 0;
    return feed_until(&s, ax, -2000, 0, GY_INPUT_SELECT, 1200) >= 0
           || s.in.direction == GY_RIGHT;
}

static void m_switch_threshold(void)
{
    static const int pcts[] = {0, 10, 50};
    int pi, r;
    for (pi = 0; pi < (int)(sizeof pcts / sizeof pcts[0]); ++pi) {
        int found = -1;
        for (r = 0; r <= 200; ++r) {
            if (switches_at(5, pcts[pi], r)) { found = r; break; }
        }
        /* 期望：新轴需强于 (100 + dir_pct)%，即最小整数 r = dir_pct + 1 */
        printf("switch_threshold,5,dir_pct=%d,%d,%d,%%\n",
               pcts[pi], found, pcts[pi] + 1);
    }
}

static void m_switch_latency(int trials)
{
    static const int pcts[] = {0, 10, 50};
    int pi, k;
    for (pi = 0; pi < (int)(sizeof pcts / sizeof pcts[0]); ++pi) {
        long sum = 0;
        int ok = 0;
        int ratio = pcts[pi] + 30;          /* 明确高于阈值 */
        int ax = 2000 + (2000 * ratio) / 100;
        for (k = 0; k < trials; ++k) {
            Sim s;
            int lat;
            sim_init(&s, 5, pcts[pi], 10, 2);
            if (establish_up(&s, 2000) < 0) continue;
            /* 切换后方向会重新经过 200ms 稳定窗口才 SELECT */
            lat = feed_until(&s, ax, -2000, 0, GY_INPUT_SELECT, 2000);
            if (lat >= 0) { sum += lat; ++ok; }
        }
        printf("switch_latency,5,dir_pct=%d,%ld,>=200,ms\n",
               pcts[pi], ok ? sum / ok : -1);
    }
}

/* ---- M3：按键到 CONFIRM 的延迟 ---- */
static void m_confirm_latency(int trials)
{
    static const int steps[] = {1, 5, 20};
    int si, k;
    for (si = 0; si < (int)(sizeof steps / sizeof steps[0]); ++si) {
        long sum = 0;
        int ok = 0;
        for (k = 0; k < trials; ++k) {
            Sim s;
            int lat;
            sim_init(&s, steps[si], 10, 10, 2);
            if (establish_up(&s, 2000) < 0) continue;
            lat = feed_until(&s, 0, -2000, 1, GY_INPUT_CONFIRM, 500);
            if (lat >= 0) { sum += lat; ++ok; }
        }
        /* 期望：>= 20ms 按键消抖 */
        printf("confirm_latency,%d,dir_pct=10,%ld,>=20,ms\n",
               steps[si], ok ? sum / ok : -1);
    }
}

/* ---- M4：误触率与"最小保持时长" ---- */
/*
 * 模型说明（前三个用于证明"不误触"，第四个是正对照）：
 *   0 static_noise          静止微抖，幅度低于 1000 的门限
 *   1 above_threshold       幅度越阈，但方向每样本随机
 *   2 single_impulse        只有第一个样本越阈的冲击
 *   3 sustained_tilt        持续定向倾斜（正对照：必须 100% 触发，否则说明工具本身失效）
 * 返回 1 表示出现过 SELECT 或 CANCEL。
 */
static int trial(int step, int model, int samples, int hold_ms)
{
    Sim s;
    int i, events = 0, held = 0;
    int hold_cycles = step > 0 ? hold_ms / step : 0;

    sim_init(&s, step, 10, 10, 2);
    settle_level(&s, 6);
    for (i = 0; i < samples; ++i) {
        int x = 0, y = 0;
        if (model == 0) {
            x = rnd_range(-900, 900);
            y = rnd_range(-900, 900);
        } else if (model == 1) {
            x = rnd_range(-1400, 1400);
            y = rnd_range(-1400, 1400);
        } else if (model == 2) {
            if (i == 0) y = -2500;
        } else {
            if (held < hold_cycles) { y = -2000; ++held; }
        }
        events |= sim_feed(&s, x, y, 0).events;
    }
    return (events & (GY_INPUT_SELECT | GY_INPUT_CANCEL)) ? 1 : 0;
}

static void m_false_trigger(int trials)
{
    static const char *names[] = {
        "static_noise", "above_threshold_noise", "single_impulse", "sustained_tilt(control)"
    };
    int model, k;
    for (model = 0; model < 4; ++model) {
        int hit = 0;
        for (k = 0; k < trials; ++k) hit += trial(5, model, 200, 300);
        printf("trigger_rate_bp,5,model=%s,%d,%d,bp(1/10000)\n",
               names[model], trials ? hit * 10000 / trials : 0,
               model == 3 ? 10000 : 0);
    }
}

/* 定向保持多久才会被认作一次有效操作（边界扫描） */
static void m_hold_boundary(int trials, int step)
{
    static const int holds[] = {50, 100, 150, 180, 200, 220, 250, 300};
    int hi, k;
    for (hi = 0; hi < (int)(sizeof holds / sizeof holds[0]); ++hi) {
        int hit = 0;
        for (k = 0; k < trials; ++k) hit += trial(step, 3, 400, holds[hi]);
        printf("hold_boundary_bp,%d,hold_ms=%d,%d,200,bp(1/10000)\n",
               step, holds[hi], trials ? hit * 10000 / trials : 0);
    }
}

/* 手持抖动模型：已选中 UP(约 22 度) 后两轴各加噪声，统计"预览可用时间占比"。
 * 这在界面上就是"黄色指示点/预览是否稳定存在"，直接对应用户说的
 * "跳一下就恢复原位置、多数时候没反应"。占比越高越好。 */
static void m_preview_availability(int trials, int step)
{
    static const int pcts[] = {0, 10, 20, 25, 30, 50};
    static const int samples = 200;          /* 200 x 5ms = 1 秒 */
    int pi, k;
    for (pi = 0; pi < (int)(sizeof pcts / sizeof pcts[0]); ++pi) {
        long shown = 0, total = 0;
        for (k = 0; k < trials; ++k) {
            Sim s;
            int i;
            sim_init(&s, step, pcts[pi], 10, 2);
            settle_level(&s, 6);
            if (feed_until(&s, 0, -2200, 0, GY_INPUT_SELECT, 2000) < 0) continue;
            for (i = 0; i < samples; ++i) {
                int x = rnd_range(-2400, 2400);
                int y = -2200 + rnd_range(-400, 400);
                sim_feed(&s, x, y, 0);
                ++total;
                if (s.in.direction >= 0) ++shown;
            }
        }
        printf("preview_available_bp,%d,dir_pct=%d,%ld,10000,bp\n",
               step, pcts[pi], total ? shown * 10000 / total : 0);
    }
}

int main(int argc, char **argv)
{
    int trials = 50;
    uint32_t seed = 20261106;
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else {
            fprintf(stderr, "usage: geyi-input-bench [--trials N] [--seed N]\n");
            return 2;
        }
    }
    if (trials < 1) trials = 1;
    seed_rng(seed);

    printf("metric,step_ms,param,value,expected,unit\n");
    m_select_latency(trials);
    m_switch_threshold();
    m_switch_latency(trials);
    m_confirm_latency(trials);
    m_false_trigger(trials);
    m_hold_boundary(trials, 5);
    m_hold_boundary(trials, 20);
    m_preview_availability(trials, 5);
    return 0;
}
