/*
 * test_config.c —— 参数配置子系统主机端测试（TC-401~405，REQ-401~411 的主机部分）
 *
 * 运行：python tools/run_host.py   （会编译并执行本文件）
 * 不依赖 LVGL、RT-Thread 或任何板级头文件，不需要开发板。
 */
#include "geyi_config.h"
#include "geyi_game.h"
#include "geyi_input.h"
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL line %d: %s\n",__LINE__,#x); return 1;} } while(0)

/* TC-401：10 项参数的元信息齐全、范围自洽 */
static int test_meta(void)
{
    int i, named = 0;
    /* 参数总数为 10 由 geyi_config.c 的编译期断言保证；这里做运行期核对：
     * 每一项都必须能在元信息表里查到名字，否则说明枚举与表不同步。 */
    for (i = 0; i < GY_P_COUNT; ++i)
        if (gy_param_name(i)[0] != '\0') ++named;
    CHECK(named == GY_P_COUNT);
    for (i = 0; i < GY_P_COUNT; ++i)
    {
        int lo = 0, hi = 0;
        CHECK(gy_param_name(i)[0] != '\0');                 /* 每项都有名字 */
        CHECK(gy_param_range(i, &lo, &hi) == GY_PARAM_OK);
        CHECK(lo <= hi);                                     /* 范围自洽 */
    }
    /* 范围宏与范围表必须一致（防止表与宏各写一套） */
    {
        int lo = 0, hi = 0;
        gy_param_range(GY_P_SIZE, &lo, &hi);
        CHECK(lo == GY_SIZE_MIN && hi == GY_SIZE_MAX);
        gy_param_range(GY_P_SPAWN_INTERVAL, &lo, &hi);
        CHECK(lo == GY_SPAWN_MIN && hi == GY_SPAWN_MAX);
        gy_param_range(GY_P_SEARCH_DEPTH, &lo, &hi);
        CHECK(lo == GY_DEPTH_MIN && hi == GY_DEPTH_MAX);
        gy_param_range(GY_P_DIR_BUFFER, &lo, &hi);
        CHECK(lo == GY_BUFFER_MIN && hi == GY_BUFFER_MAX);
    }
    return 0;
}

/* TC-401：三类默认值都在范围内且通过校验 */
static int test_defaults(void)
{
    int mode, i;
    for (mode = 0; mode < GY_MODE_COUNT; ++mode)
    {
        GyParamSet s = gy_param_default_for(mode);
        for (i = 0; i < GY_P_COUNT; ++i)
            CHECK(gy_param_validate(i, gy_param_get(&s, i)) == GY_PARAM_OK);
    }
    /* 默认值必须等于改造前的硬编码值，保证参数化前后行为一致 */
    {
        GyParamSet s = gy_param_default_for(GY_MODE_SINGLE);
        CHECK(s.size == 6 && s.hp == 8 && s.attack == 2 && s.cost == 4);
        CHECK(s.merge_reward == 1 && s.kill_reward == 2);
        CHECK(s.spawn_interval == 1);      /* 改造前：每次移动都生成 */
        CHECK(s.dir_buffer == 10 && s.angle_buffer == 10);  /* 改造前硬编码 10% */
    }
    return 0;
}

/* TC-401/405：转出的 GyConfig 必须被 gy_init 接受（与规则库的校验对齐） */
static int test_to_game_config(void)
{
    int mode;
    for (mode = 0; mode < GY_MODE_COUNT; ++mode)
    {
        GyGame g;
        GyConfig c;
        int i;
        GyParamSet s = gy_param_default_for(mode);

        CHECK(gy_param_apply(mode, &c) == 1);
        CHECK(c.size == s.size && c.hp == s.hp && c.attack == s.attack);
        CHECK(c.cost == s.cost && c.merge_reward == s.merge_reward);
        CHECK(c.kill_reward == s.kill_reward);
        CHECK(c.spawn_interval == s.spawn_interval);
        CHECK(gy_init(&g, c, 123) == 1);   /* 规则库接受该配置 */

        /* 每项取范围内的每个值，转出的配置都应在 gy_init 的可接受区间内 */
        for (i = 0; i < GY_P_COUNT; ++i)
        {
            int lo = 0, hi = 0, v;
            gy_param_range(i, &lo, &hi);
            for (v = lo; v <= hi; ++v)
            {
                int rc = gy_param_validate(i, v);
                if (i == GY_P_SEARCH_DEPTH ||
                    i == GY_P_DIR_BUFFER || i == GY_P_ANGLE_BUFFER)
                    continue;   /* 这三项不进 GyConfig */
                if (rc == GY_PARAM_OK)
                {
                    GyParamSet t = s;
                    GyConfig tc;
                    CHECK(gy_param_set(&t, i, v) == GY_PARAM_OK);
                    gy_param_to_game_config(&t, &tc);
                    CHECK(gy_init(&g, tc, 7) == 1);
                }
            }
        }
    }
    return 0;
}

/* TC-402：每个参数在 0 / 1 / 下限−1 / 上限 / 上限+1 上的 validate 与 clamp 行为 */
static int test_bounds(void)
{
    int i;
    for (i = 0; i < GY_P_COUNT; ++i)
    {
        int lo = 0, hi = 0, v;
        int probes[5];
        gy_param_range(i, &lo, &hi);
        probes[0] = 0;
        probes[1] = 1;
        probes[2] = lo - 1;
        probes[3] = hi;
        probes[4] = hi + 1;

        {
            int k;
            for (k = 0; k < 5; ++k)
            {
                int probe = probes[k];
                int rc = gy_param_validate(i, probe);
                int cv = gy_param_clamp(i, probe);
                /* 钳位结果必须始终落在范围内 */
                CHECK(cv >= lo && cv <= hi);
                /* 越界时 validate 必须明确报 RANGE，而不是笼统失败 */
                if (probe < lo || probe > hi)
                    CHECK(rc == GY_PARAM_ERR_RANGE);
                /* 在范围内时，只有"必须奇数"那一项可能被拒 */
                if (probe >= lo && probe <= hi && rc != GY_PARAM_OK)
                    CHECK(rc == GY_PARAM_ERR_ODD);
                v = cv;
                (void)v;
            }
        }

        /* 极端越界值也必须被钳到边界 */
        CHECK(gy_param_clamp(i, lo - 1000) == lo);
        CHECK(gy_param_clamp(i, hi + 1000) == hi);
    }
    return 0;
}

/* TC-402：set 必须"钳位写入 + 如实返回原始输入的错误码" */
static int test_set_clamps_but_reports(void)
{
    GyParamSet s = gy_param_default_for(GY_MODE_SINGLE);
    CHECK(gy_param_set(&s, GY_P_SIZE, 99) == GY_PARAM_ERR_RANGE);
    CHECK(s.size == GY_SIZE_MAX);                 /* 写入的是钳位后的值 */
    CHECK(gy_param_set(&s, GY_P_SIZE, 0) == GY_PARAM_ERR_RANGE);
    CHECK(s.size == GY_SIZE_MIN);
    CHECK(gy_param_set(&s, GY_P_SIZE, 6) == GY_PARAM_OK);
    CHECK(s.size == 6);
    return 0;
}

/* TC-405：生成间隔必须为奇数 1~9 */
static int test_spawn_odd(void)
{
    GyParamSet s = gy_param_default_for(GY_MODE_SINGLE);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 1) == GY_PARAM_OK);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 3) == GY_PARAM_OK);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 9) == GY_PARAM_OK);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 2) == GY_PARAM_ERR_ODD);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 4) == GY_PARAM_ERR_ODD);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 8) == GY_PARAM_ERR_ODD);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 0) == GY_PARAM_ERR_RANGE);
    CHECK(gy_param_validate(GY_P_SPAWN_INTERVAL, 10) == GY_PARAM_ERR_RANGE);
    /* 偶数输入钳位后必须是奇数且在范围内 */
    CHECK(gy_param_clamp(GY_P_SPAWN_INTERVAL, 2) % 2 == 1);
    CHECK(gy_param_clamp(GY_P_SPAWN_INTERVAL, 4) % 2 == 1);
    CHECK(gy_param_clamp(GY_P_SPAWN_INTERVAL, 8) % 2 == 1);
    /* set 写入后一定是合法奇数 */
    CHECK(gy_param_set(&s, GY_P_SPAWN_INTERVAL, 4) == GY_PARAM_ERR_ODD);
    CHECK(s.spawn_interval % 2 == 1);
    return 0;
}

/* TC-403：三类配置互相独立；search_depth 仅人机模式适用 */
static int test_modes(void)
{
    CHECK(gy_param_valid_in_mode(GY_P_SEARCH_DEPTH, GY_MODE_PVE) == 1);
    CHECK(gy_param_valid_in_mode(GY_P_SEARCH_DEPTH, GY_MODE_SINGLE) == 0);
    CHECK(gy_param_valid_in_mode(GY_P_SEARCH_DEPTH, GY_MODE_PVP) == 0);
    /* 其余参数在三种模式下都适用 */
    CHECK(gy_param_valid_in_mode(GY_P_SIZE, GY_MODE_SINGLE) == 1);
    CHECK(gy_param_valid_in_mode(GY_P_SIZE, GY_MODE_PVP) == 1);

    CHECK(gy_param_init() == GY_PARAM_OK);
    CHECK(gy_param_mode(GY_MODE_SINGLE) != NULL);
    CHECK(gy_param_mode(GY_MODE_PVE) != NULL);
    CHECK(gy_param_mode(GY_MODE_PVP) != NULL);
    CHECK(gy_param_mode(GY_MODE_COUNT) == NULL);      /* 非法模式 */
    CHECK(gy_param_mode(-1) == NULL);

    /* 改单机不得影响人机与联机 */
    gy_param_mode(GY_MODE_SINGLE)->size = 5;
    gy_param_mode(GY_MODE_SINGLE)->hp = 32;
    CHECK(gy_param_mode(GY_MODE_PVE)->size == 6);
    CHECK(gy_param_mode(GY_MODE_PVP)->size == 6);
    CHECK(gy_param_mode(GY_MODE_PVE)->hp == 8);
    /* 改人机不得影响单机 */
    gy_param_mode(GY_MODE_PVE)->search_depth = 6;
    CHECK(gy_param_mode(GY_MODE_SINGLE)->search_depth == 3);
    return 0;
}

/* TC-404：重置回官方默认，且幂等 */
static int test_reset(void)
{
    int mode, i;
    for (mode = 0; mode < GY_MODE_COUNT; ++mode)
    {
        GyParamSet d = gy_param_default_for(mode);
        GyParamSet *p;
        CHECK(gy_param_init() == GY_PARAM_OK);
        p = gy_param_mode(mode);
        /* 把每一项都改成非默认值 */
        p->size = 4;
        p->spawn_interval = 9;
        p->hp = 64;
        p->attack = 16;
        p->cost = 20;
        p->merge_reward = 5;
        p->kill_reward = 10;
        p->search_depth = 1;
        p->dir_buffer = 0;
        p->angle_buffer = 50;

        CHECK(gy_param_reset(mode) == GY_PARAM_OK);
        for (i = 0; i < GY_P_COUNT; ++i)
            CHECK(gy_param_get(gy_param_mode(mode), i) == gy_param_get(&d, i));

        /* 幂等：再重置一次结果相同 */
        CHECK(gy_param_reset(mode) == GY_PARAM_OK);
        for (i = 0; i < GY_P_COUNT; ++i)
            CHECK(gy_param_get(gy_param_mode(mode), i) == gy_param_get(&d, i));
    }
    CHECK(gy_param_reset(GY_MODE_COUNT) == GY_PARAM_ERR_ID);
    return 0;
}

/* 读写往返：10 项逐一写入合法值后能读回 */
static int test_get_set_roundtrip(void)
{
    int i, mode;
    for (mode = 0; mode < GY_MODE_COUNT; ++mode)
    {
        GyParamSet s = gy_param_default_for(mode);
        for (i = 0; i < GY_P_COUNT; ++i)
        {
            int lo = 0, hi = 0, v;
            gy_param_range(i, &lo, &hi);
            for (v = lo; v <= hi; ++v)
            {
                if (gy_param_validate(i, v) != GY_PARAM_OK) continue;
                CHECK(gy_param_set(&s, i, v) == GY_PARAM_OK);
                CHECK(gy_param_get(&s, i) == v);
            }
        }
    }
    return 0;
}

/* 非法标识必须报错，不能静默通过 */
static int test_invalid_id(void)
{
    GyParamSet s = gy_param_default_for(GY_MODE_SINGLE);
    CHECK(gy_param_validate(-1, 1) == GY_PARAM_ERR_ID);
    CHECK(gy_param_validate(GY_P_COUNT, 1) == GY_PARAM_ERR_ID);
    CHECK(gy_param_range(-1, NULL, NULL) == GY_PARAM_ERR_ID);
    CHECK(gy_param_name(-1)[0] == '\0');
    CHECK(gy_param_set(&s, -1, 1) == GY_PARAM_ERR_ID);
    CHECK(gy_param_set(NULL, GY_P_SIZE, 6) == GY_PARAM_ERR_ID);
    return 0;
}

/* 子系统自检入口 */
static int test_selftest(void)
{
    CHECK(gy_param_selftest() == 0);
    return 0;
}

/* TC-406（主机部分）：生成间隔的判定逻辑；板端只需确认画面与手感 */
static int test_spawn_interval_effect(void)
{
    GyGame g;
    GyConfig c = gy_default_config();

    /* 间隔 1（默认）→ 每次有效移动后都生成，与改造前行为一致 */
    c.spawn_interval = 1;
    CHECK(gy_init(&g, c, 42) == 1);
    CHECK(gy_should_spawn(&g) == 0);        /* 尚未发生有效移动 */
    g.moves = 1; CHECK(gy_should_spawn(&g) == 1);
    g.moves = 2; CHECK(gy_should_spawn(&g) == 1);
    g.moves = 7; CHECK(gy_should_spawn(&g) == 1);

    /* 间隔 3 → 仅第 3/6/9 次有效移动后生成 */
    c.spawn_interval = 3;
    CHECK(gy_init(&g, c, 42) == 1);
    g.moves = 1; CHECK(gy_should_spawn(&g) == 0);
    g.moves = 2; CHECK(gy_should_spawn(&g) == 0);
    g.moves = 3; CHECK(gy_should_spawn(&g) == 1);
    g.moves = 4; CHECK(gy_should_spawn(&g) == 0);
    g.moves = 5; CHECK(gy_should_spawn(&g) == 0);
    g.moves = 6; CHECK(gy_should_spawn(&g) == 1);
    g.moves = 9; CHECK(gy_should_spawn(&g) == 1);

    /* 防御：非法间隔（<=0）按"每次生成"处理，且绝不除零 */
    c.spawn_interval = 0;
    CHECK(gy_init(&g, c, 42) == 1);
    g.moves = 5; CHECK(gy_should_spawn(&g) == 1);
    c.spawn_interval = -3;
    CHECK(gy_init(&g, c, 42) == 1);
    g.moves = 5; CHECK(gy_should_spawn(&g) == 1);

    /* 放置士兵不推进有效移动计数，因此不影响生成间隔（沿用 REQ-301） */
    c.spawn_interval = 3;
    CHECK(gy_init(&g, c, 42) == 1);
    {
        uint32_t moves_before = g.moves;
        g.coins[g.turn] = 20;
        CHECK(gy_place(&g, 2) == 1);            /* 己方底线第 2 列为空 */
        CHECK(g.moves == moves_before);         /* 放置不增 moves */
        CHECK(gy_should_spawn(&g) == 0);        /* 也就不推进生成计数 */
    }
    return 0;
}

/* 集成回归：gy_simulate_move 的 spawn_next=1 必须遵守 spawn_interval，
 * 否则 AI 搜索会按“每次生成”模拟，与真实对局不一致。 */
static int count_numbers(const GyGame *g)
{
    int n = 0, x, y;
    for (y = 0; y < g->config.size; ++y)
        for (x = 0; x < g->config.size; ++x)
            n += g->cells[y][x].kind == GY_NUMBER;
    return n;
}

static int setup_spawn_board(GyGame *g, int interval)
{
    GyConfig c = gy_default_config();
    GyCell cell;
    c.spawn_interval = interval;
    if (gy_init(g, c, 42) != 1) return 0;
    memset(g->cells, 0, sizeof(g->cells));
    cell.kind = GY_SOLDIER; cell.owner = 0; cell.hp = 8; cell.value = 0;
    g->cells[1][3] = cell;
    cell.kind = GY_NUMBER; cell.owner = 0; cell.hp = 0; cell.value = 2;
    g->cells[1][1] = cell;
    cell.kind = GY_SOLDIER; cell.owner = 1; cell.hp = 8; cell.value = 0;
    g->cells[5][5] = cell;
    g->turn = 0;
    g->outcome = GY_PLAYING;
    g->moves = 0;
    g->coins[0] = g->coins[1] = 0;
    return 1;
}

static int test_simulate_respects_spawn_interval(void)
{
    GyGame g, next;
    GyMove m = {GY_RIGHT, 1};

    /* 间隔 3：第 1 次有效移动不生成 */
    CHECK(setup_spawn_board(&g, 3));
    CHECK(gy_simulate_move(&g, m, &next, 1) == 1);
    CHECK(next.moves == 1);
    CHECK(count_numbers(&next) == 1);

    /* 间隔 3：第 3 次有效移动生成 */
    CHECK(setup_spawn_board(&g, 3));
    g.moves = 2;
    CHECK(gy_simulate_move(&g, m, &next, 1) == 1);
    CHECK(next.moves == 3);
    CHECK(count_numbers(&next) == 2);

    /* spawn_next=0（AI 搜索）永远不生成 */
    CHECK(setup_spawn_board(&g, 3));
    g.moves = 2;
    CHECK(gy_simulate_move(&g, m, &next, 0) == 1);
    CHECK(count_numbers(&next) == 1);

    /* 间隔 1（默认）每次有效移动都生成 */
    CHECK(setup_spawn_board(&g, 1));
    CHECK(gy_simulate_move(&g, m, &next, 1) == 1);
    CHECK(count_numbers(&next) == 2);
    return 0;
}

/* TC-402/TC-409：倾斜缓冲阈值已接线到 geyi_input，并保持默认 10% 不改变既有行为 */
static int test_input_buffer_wired(void)
{
    GyInput s;
    int a[4] = {2, 2, 2, 2};
    uint32_t t;

    /* 默认值必须是 10%（等同改造前的硬编码），否则既有输入行为会变 */
    gy_input_reset(&s);
    CHECK(s.dir_buffer_pct == 10);
    CHECK(s.angle_buffer_pct == 10);

    /* 配置接口按区间钳位 */
    gy_input_configure(&s, -5, 999);
    CHECK(s.dir_buffer_pct == GY_INPUT_BUFFER_MIN);
    CHECK(s.angle_buffer_pct == GY_INPUT_BUFFER_MAX);
    gy_input_configure(&s, 0, 0);
    CHECK(s.dir_buffer_pct == 0 && s.angle_buffer_pct == 0);

    /* 两处范围定义必须一致，防止 geyi_config 与 geyi_input 各写一套后漂移。
     * 这里从 geyi_config 的**范围表**读取（而不是比宏），才能真正发现表与输入模块不一致。 */
    {
        int cfg_min = 0, cfg_max = 0;
        CHECK(gy_param_range(GY_P_DIR_BUFFER, &cfg_min, &cfg_max) == GY_PARAM_OK);
        CHECK(cfg_min == GY_INPUT_BUFFER_MIN);
        CHECK(cfg_max == GY_INPUT_BUFFER_MAX);
        CHECK(gy_param_range(GY_P_ANGLE_BUFFER, &cfg_min, &cfg_max) == GY_PARAM_OK);
        CHECK(cfg_min == GY_INPUT_BUFFER_MIN);
        CHECK(cfg_max == GY_INPUT_BUFFER_MAX);
    }

    /* 行为验证：纵轴 2000 已稳定后，用横轴 2100（强 5%）试探。
     * 阈值 0%  → 应当切换到横轴；阈值 50% → 应当保持原方向。
     * 注意：输入模块要求先"回中"解除 latched（见 gy_input_selftest 的做法），
     * 所以每段都以水平样例开头，不能直接上倾斜。 */
    {
        int dir_switch, dir_hold;

        /* 阈值 0%：稍强一点的横轴就应切走 */
        gy_input_reset(&s);
        gy_input_configure(&s, 0, 10);
        t = 0;
        for (; t <= 30; t += 5)  gy_input_update(&s, 0, 0, 0, t, a);        /* 回中，解除 latched */
        for (; t <= 280; t += 5) gy_input_update(&s, 0, -2000, 0, t, a);    /* 稳定指向 UP */
        CHECK(s.direction == GY_UP);
        for (; t <= 580; t += 5) gy_input_update(&s, 2100, -2000, 0, t, a); /* 指向 RIGHT，仅强 5% */
        dir_switch = s.direction;

        /* 阈值 50%：完全相同的输入应保持原方向 */
        gy_input_reset(&s);
        gy_input_configure(&s, 50, 10);
        t = 0;
        for (; t <= 30; t += 5)  gy_input_update(&s, 0, 0, 0, t, a);
        for (; t <= 280; t += 5) gy_input_update(&s, 0, -2000, 0, t, a);
        CHECK(s.direction == GY_UP);
        for (; t <= 580; t += 5) gy_input_update(&s, 2100, -2000, 0, t, a);
        dir_hold = s.direction;

        CHECK(dir_switch == GY_RIGHT);      /* 阈值 0 时切走 */
        CHECK(dir_hold == GY_UP);           /* 阈值 50 时保持 */
    }
    return 0;
}

int main(void)
{
    if (test_meta()) return 1;
    if (test_defaults()) return 1;
    if (test_to_game_config()) return 1;
    if (test_bounds()) return 1;
    if (test_set_clamps_but_reports()) return 1;
    if (test_spawn_odd()) return 1;
    if (test_modes()) return 1;
    if (test_reset()) return 1;
    if (test_get_set_roundtrip()) return 1;
    if (test_invalid_id()) return 1;
    if (test_spawn_interval_effect()) return 1;
    if (test_simulate_respects_spawn_interval()) return 1;
    if (test_input_buffer_wired()) return 1;
    if (test_selftest()) return 1;
    printf("PASS: %d assertions; TC-401..TC-406 + TC-409 host tests\n", checks);
    return 0;
}
