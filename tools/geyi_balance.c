/*
 * geyi_balance.c —— REQ-411 平衡扫描的核心实现（主机端）
 *
 * 这是“工具内基线贪心策略”，不是 AI 模块：每个候选动作只按即时收益打分，
 * 不做搜索。参数网格、种子集合和回合上限固定，因此同一版本重复运行可逐字节复现。
 */
#include "geyi_balance.h"
#include <stdio.h>
#include <string.h>

/* GyConfig 字段顺序：size, hp, attack, cost, merge_reward, kill_reward, spawn_interval。
 * 5 种棋盘 × 4 组规则 = 20 组，覆盖大小/生成间隔/血量/攻击/费用/奖励。 */
static const GyConfig k_groups[] = {
    {4,  8, 2, 4, 1, 2, 1},
    {4,  8, 2, 4, 1, 2, 3},
    {4, 12, 3, 5, 2, 3, 1},
    {4,  6, 1, 3, 1, 2, 5},
    {5,  8, 2, 4, 1, 2, 1},
    {5,  8, 2, 4, 1, 2, 3},
    {5, 12, 3, 5, 2, 3, 1},
    {5,  6, 1, 3, 1, 2, 5},
    {6,  8, 2, 4, 1, 2, 1},
    {6,  8, 2, 4, 1, 2, 3},
    {6, 12, 3, 5, 2, 3, 1},
    {6,  6, 1, 3, 1, 2, 5},
    {7,  8, 2, 4, 1, 2, 1},
    {7,  8, 2, 4, 1, 2, 3},
    {7, 12, 3, 5, 2, 3, 1},
    {7,  6, 1, 3, 1, 2, 5},
    {8,  8, 2, 4, 1, 2, 1},
    {8,  8, 2, 4, 1, 2, 3},
    {8, 12, 3, 5, 2, 3, 1},
    {8,  6, 1, 3, 1, 2, 5},
};

int gy_balance_group_count(void)
{
    return (int)(sizeof(k_groups) / sizeof(k_groups[0]));
}

static long score_after(const GyGame *before, const GyGame *after, int turn)
{
    long score;
    int x, y;
    if (after->outcome != GY_PLAYING) {
        if (after->outcome == GY_DRAW) return 0;
        if ((after->outcome == GY_RED_WIN && turn == 0) ||
            (after->outcome == GY_BLUE_WIN && turn == 1)) return 1000000L;
        return -1000000L;
    }
    score = (long)(after->coins[turn] - before->coins[turn]) * 30L;
    for (y = 0; y < after->config.size; ++y) {
        for (x = 0; x < after->config.size; ++x) {
            const GyCell *c = &after->cells[y][x];
            if (c->kind == GY_SOLDIER) {
                score += c->owner == turn ? (long)c->hp * 6L : -(long)c->hp * 8L;
            }
        }
    }
    return score;
}

typedef struct {
    int kind;              /* 1=move, 2=place, 0=none */
    GyMove move;
    int column;
    long score;
} Candidate;

static Candidate best_candidate(const GyGame *g)
{
    Candidate best;
    int turn = g->turn;
    int d, count;
    best.kind = 0;
    best.score = -2000000L;
    for (d = 0; d < 4; ++d) {
        int available = gy_eligible_count(g, d);
        for (count = 0; count <= available; ++count) {
            GyMove move;
            GyGame next;
            long score;
            move.direction = d;
            move.count = count;
            if (!gy_simulate_move(g, move, &next, 0)) continue;
            score = score_after(g, &next, turn);
            if (best.kind == 0 || score > best.score) {
                best.kind = 1;
                best.move = move;
                best.score = score;
            }
        }
    }
    if (g->coins[turn] >= g->config.cost) {
        int row = turn == 0 ? g->config.size - 1 : 0;
        int col;
        for (col = 0; col < g->config.size; ++col) {
            GyGame next;
            long score;
            if (g->cells[row][col].kind != GY_EMPTY) continue;
            next = *g;
            if (!gy_place(&next, col)) continue;
            score = score_after(g, &next, turn);
            if (best.kind == 0 || score > best.score) {
                best.kind = 2;
                best.column = col;
                best.score = score;
            }
        }
    }
    return best;
}

static void play_game(const GyConfig *cfg, uint32_t seed, int *red, int *blue, int *draw, int *moves)
{
    GyGame g;
    int turns;
    if (!gy_init(&g, *cfg, seed)) { ++*draw; return; }
    for (turns = 0; turns < 2000 && g.outcome == GY_PLAYING; ++turns) {
        Candidate c = best_candidate(&g);
        if (c.kind == 1) {
            GyPreview p;
            if (!gy_preview(&g, c.move, &p) || !gy_commit(&g, &p)) {
                gy_resign(&g);
                break;
            }
        } else if (c.kind == 2) {
            if (!gy_place(&g, c.column)) {
                gy_resign(&g);
                break;
            }
        } else {
            gy_resign(&g);
            break;
        }
    }
    *moves += (int)g.moves;
    if (g.outcome == GY_RED_WIN) ++*red;
    else if (g.outcome == GY_BLUE_WIN) ++*blue;
    else ++*draw;
}

int gy_balance_scan_csv(int group_count, int games_per_group, char *out, size_t out_size)
{
    int group, game;
    size_t used = 0;
    int n;
    if (!out || out_size == 0 || group_count < 1 ||
        group_count > gy_balance_group_count() || games_per_group < 1) return -1;
    n = snprintf(out + used, out_size - used,
                 "group,size,spawn_interval,hp,attack,cost,merge_reward,kill_reward,games,red_wins,blue_wins,draws,avg_moves\n");
    if (n < 0 || (size_t)n >= out_size - used) return -1;
    used += (size_t)n;
    for (group = 0; group < group_count; ++group) {
        const GyConfig *cfg = &k_groups[group];
        int red = 0, blue = 0, draw = 0, moves = 0;
        for (game = 0; game < games_per_group; ++game) {
            uint32_t seed = 20261028u + (uint32_t)group * 100000u + (uint32_t)game * 7919u;
            play_game(cfg, seed, &red, &blue, &draw, &moves);
        }
        n = snprintf(out + used, out_size - used,
                     "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f\n",
                     group, cfg->size, cfg->spawn_interval, cfg->hp, cfg->attack,
                     cfg->cost, cfg->merge_reward, cfg->kill_reward,
                     games_per_group, red, blue, draw,
                     games_per_group ? (double)moves / games_per_group : 0.0);
        if (n < 0 || (size_t)n >= out_size - used) return -1;
        used += (size_t)n;
    }
    return 0;
}
