#include "geyi_game.h"
#include <string.h>
#include <stdlib.h>

static const int dx[4] = {0, 1, 0, -1};
static const int dy[4] = {-1, 0, 1, 0};
static int inside(const GyGame *g, int x, int y)
{ return x >= 0 && y >= 0 && x < g->config.size && y < g->config.size; }
static uint32_t random_next(GyGame *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x; return x;
}
static void add_coins(GyGame *g, int owner, int amount)
{
    int sum = g->coins[owner] + amount;
    g->coins[owner] = sum > 1000000 ? 1000000 : sum;
}
static void spawn(GyGame *g)
{
    int slots[GY_MAX * GY_MAX], n = 0, x, y, index;
    for (y = 0; y < g->config.size; ++y)
        for (x = 0; x < g->config.size; ++x)
            if (g->cells[y][x].kind == GY_EMPTY) slots[n++] = y * GY_MAX + x;
    if (!n) return;
    index = slots[random_next(g) % (uint32_t)n];
    g->cells[index / GY_MAX][index % GY_MAX].kind = GY_NUMBER;
    g->cells[index / GY_MAX][index % GY_MAX].value = random_next(g) % 10 == 0 ? 4 : 2;
}
GyConfig gy_default_config(void)
{
    GyConfig c = {6, 8, 2, 4, 1, 2, 1}; return c;
}
int gy_init(GyGame *g, GyConfig c, uint32_t seed)
{
    int p, i;
    if (!g || c.size < 4 || c.size > GY_MAX || c.hp < 1 || c.hp > 10000 ||
        c.attack < 1 || c.attack > 10000 || c.cost < 1 || c.cost > 10000 ||
        c.merge_reward < 0 || c.merge_reward > 10000 ||
        c.kill_reward < 0 || c.kill_reward > 10000) return 0;
    memset(g, 0, sizeof(*g)); g->config = c; g->rng = seed ? seed : 1;
    for (p = 0; p < 2; ++p) for (i = 0; i < 2; ++i) {
        GyCell *cell = &g->cells[p == 0 ? c.size - 1 : 0][i == 0 ? 1 : c.size - 2];
        cell->kind = GY_SOLDIER; cell->owner = p; cell->hp = c.hp;
    }
    spawn(g); spawn(g); return 1;
}
/* Traverse from the destination edge, independent of frame rate. */
static void position(int d, int n, int line, int step, int *x, int *y)
{
    *x = (d == GY_LEFT || d == GY_RIGHT) ? (d == GY_LEFT ? step : n - 1 - step) : line;
    *y = (d == GY_UP || d == GY_DOWN) ? (d == GY_UP ? step : n - 1 - step) : line;
}
static void move_number(GyGame *g, int d, int x, int y,
                        unsigned char merged[GY_MAX][GY_MAX])
{
    int nx, ny;
    GyCell item = g->cells[y][x];
    memset(&g->cells[y][x], 0, sizeof(GyCell));
    for (;;) {
        GyCell *target;
        nx = x + dx[d]; ny = y + dy[d];
        if (!inside(g, nx, ny)) break;
        target = &g->cells[ny][nx];
        if (target->kind == GY_EMPTY) { x = nx; y = ny; continue; }
        if (target->kind == GY_NUMBER && target->value == item.value &&
            item.value < 65536 && !merged[ny][nx]) {
            target->value *= 2; merged[ny][nx] = 1;
            add_coins(g, g->turn, g->config.merge_reward); item.kind = GY_EMPTY;
        }
        break;
    }
    if (item.kind != GY_EMPTY) g->cells[y][x] = item;
}
static int move_soldier(GyGame *g, int d, int x, int y)
{
    int nx = x + dx[d], ny = y + dy[d];
    GyCell *from = &g->cells[y][x], *to;
    if (!inside(g, nx, ny)) return 0;
    to = &g->cells[ny][nx];
    if (to->kind == GY_EMPTY) {
        *to = *from; memset(from, 0, sizeof(*from)); return 1;
    }
    if (to->kind == GY_SOLDIER && to->owner != g->turn && to->hp > 0) {
        to->hp -= g->config.attack; return 1;
    }
    return 0;
}
/* D1: after all movement, each number hits the soldier directly in front. */
static void damage_numbers(GyGame *g, int d)
{
    int x, y, nx, ny;
    for (y = 0; y < g->config.size; ++y) for (x = 0; x < g->config.size; ++x) {
        if (g->cells[y][x].kind != GY_NUMBER) continue;
        nx = x + dx[d]; ny = y + dy[d];
        if (inside(g, nx, ny) && g->cells[ny][nx].kind == GY_SOLDIER)
            g->cells[ny][nx].hp -= (int)g->cells[y][x].value;
    }
}
/* One front-to-back pass. Numbers and selected soldiers are interleaved so a
 * piece sees cells vacated earlier in the same move. */
static void move_pass(GyGame *g, int d, const unsigned char selected[GY_MAX][GY_MAX],
                      int *actions, unsigned char acted[GY_MAX][GY_MAX])
{
    int n = g->config.size, line, step, x, y;
    unsigned char merged[GY_MAX][GY_MAX] = {{0}};
    if (actions) *actions = 0;
    for (line = 0; line < n; ++line) for (step = 0; step < n; ++step) {
        position(d, n, line, step, &x, &y);
        if (g->cells[y][x].kind == GY_NUMBER) {
            move_number(g, d, x, y, merged);
        } else if (g->cells[y][x].kind == GY_SOLDIER &&
                   g->cells[y][x].owner == g->turn &&
                   g->cells[y][x].hp > 0 && selected && selected[y][x]) {
            if (move_soldier(g, d, x, y)) {
                if (actions) ++*actions;
                if (acted) acted[y][x] = 1;
            }
        }
    }
    damage_numbers(g, d);
}
/* Simulate every own soldier selected; return how many act if all are chosen. */
static int simulate_actions(const GyGame *g, int d, unsigned char acted[GY_MAX][GY_MAX])
{
    GyGame tmp = *g;
    unsigned char all[GY_MAX][GY_MAX] = {{0}};
    int x, y, actions = 0;
    for (y = 0; y < tmp.config.size; ++y) for (x = 0; x < tmp.config.size; ++x)
        if (tmp.cells[y][x].kind == GY_SOLDIER && tmp.cells[y][x].owner == tmp.turn &&
            tmp.cells[y][x].hp > 0)
            all[y][x] = 1;
    move_pass(&tmp, d, all, &actions, acted);
    return actions;
}
int gy_eligible_count(const GyGame *g, int direction)
{
    unsigned char acted[GY_MAX][GY_MAX] = {{0}};
    if (!g || direction < 0 || direction > 3 || g->outcome != GY_PLAYING) return 0;
    return simulate_actions(g, direction, acted);
}
static void finish(GyGame *g)
{
    int alive[2] = {0}, x, y;
    for (y = 0; y < g->config.size; ++y) for (x = 0; x < g->config.size; ++x) {
        GyCell *a = &g->cells[y][x];
        if (a->kind != GY_SOLDIER) continue;
        if (a->hp <= 0) {
            if (a->owner != g->turn) add_coins(g, g->turn, g->config.kill_reward);
            memset(a, 0, sizeof(*a));
        } else ++alive[a->owner];
    }
    if (!alive[0] && !alive[1]) g->outcome = GY_DRAW;
    else if (!alive[0]) g->outcome = GY_BLUE_WIN;
    else if (!alive[1]) g->outcome = GY_RED_WIN;
}
/* Apply a move to the copy in a. before is the untouched source board used
 * only for the no-change check. Does not spawn or toggle the turn. */
static int apply_move(GyGame *a, const GyGame *before, GyMove m)
{
    int x, y, n, total = 0, ids[GY_MAX*GY_MAX], dist[GY_MAX*GY_MAX];
    int i, d, cx, cy;
    unsigned char acted[GY_MAX][GY_MAX] = {{0}};
    unsigned char selected[GY_MAX][GY_MAX] = {{0}};
    if (a->outcome != GY_PLAYING || m.direction < 0 || m.direction > 3 || m.count < 0) return 0;
    d = m.direction; n = a->config.size;
    (void)simulate_actions(a, d, acted);
    for (y = 0; y < n; ++y) for (x = 0; x < n; ++x) if (acted[y][x]) ++total;
    if (m.count > total || (m.count == 0 && total > 0)) return 0;
    /* Doubled coordinates retain half-cell edge centers without floats. */
    cx = d == GY_LEFT ? -1 : (d == GY_RIGHT ? 2*n-1 : n-1);
    cy = d == GY_UP ? -1 : (d == GY_DOWN ? 2*n-1 : n-1);
    {
        int sel_count = 0;
        for (y = 0; y < n; ++y) for (x = 0; x < n; ++x) if (acted[y][x]) {
            int distance = abs(2*x-cx) + abs(2*y-cy);
            i = sel_count++;
            while (i > 0 && dist[i-1] > distance) {
                ids[i] = ids[i-1]; dist[i] = dist[i-1]; --i;
            }
            ids[i] = y*GY_MAX+x; dist[i] = distance;
        }
        for (i = 0; i < m.count; ++i) selected[ids[i]/GY_MAX][ids[i]%GY_MAX] = 1;
    }
    move_pass(a, d, selected, NULL, NULL);
    if (memcmp(a->cells,before->cells,sizeof(before->cells)) == 0 &&
        a->coins[0] == before->coins[0] && a->coins[1] == before->coins[1]) return 0;
    finish(a); ++a->moves; ++a->revision;
    return 1;
}
int gy_preview(const GyGame *g, GyMove m, GyPreview *p)
{
    GyGame *a;
    if (!g || !p) return 0;
    memset(p, 0, sizeof(*p));
    if (g->outcome != GY_PLAYING || m.direction < 0 || m.direction > 3 || m.count < 0) return 0;
    p->before = *g; p->after = *g; a = &p->after;
    if (!apply_move(a, &p->before, m)) return 0;
    p->visible = *a; /* Never expose the next randomly spawned tile in UI preview. */
    if (a->outcome == GY_PLAYING) {
        /* 是否生成由 gy_should_spawn() 决定（抽成函数以便主机测试）。
         * 放置士兵不经过本函数，因此不计入间隔（沿用 REQ-301 的原型约定）。 */
        if (gy_should_spawn(a)) spawn(a);
        a->turn ^= 1;
    }
    p->valid = 1; return 1;
}
int gy_simulate_move(const GyGame *g, GyMove m, GyGame *next, int spawn_next)
{
    if (!g || !next || next == g) return 0;
    *next = *g;
    if (!apply_move(next, g, m)) return 0;
    if (next->outcome == GY_PLAYING) {
        /* 与 gy_preview 使用同一生成判定：spawn_next=1 时也遵守 spawn_interval，
         * 否则 AI 搜索会按“每次生成”模拟，与真实对局不一致。 */
        if (spawn_next && gy_should_spawn(next)) spawn(next);
        next->turn ^= 1;
    }
    return 1;
}
int gy_commit(GyGame *g, const GyPreview *p)
{
    if (!g || !p || !p->valid || memcmp(g,&p->before,sizeof(*g))) return 0;
    *g = p->after; return 1;
}
int gy_place(GyGame *g, int column)
{
    int row; GyCell *cell;
    if (!g || g->outcome != GY_PLAYING || column < 0 || column >= g->config.size ||
        g->coins[g->turn] < g->config.cost) return 0;
    row = g->turn == 0 ? g->config.size-1 : 0;
    cell = &g->cells[row][column];
    if (cell->kind != GY_EMPTY) return 0;
    cell->kind = GY_SOLDIER; cell->owner = g->turn; cell->hp = g->config.hp;
    g->coins[g->turn] -= g->config.cost; g->turn ^= 1; ++g->revision; return 1;
}
int gy_resign(GyGame *g)
{
    if (!g || g->outcome != GY_PLAYING) return 0;
    g->outcome = g->turn == 0 ? GY_BLUE_WIN : GY_RED_WIN; ++g->revision; return 1;
}
int gy_should_spawn(const GyGame *g)
{
    if (!g) return 0;
    if (g->moves == 0) return 0;                      /* 尚未发生有效移动，不生成 */
    if (g->config.spawn_interval <= 1) return 1;      /* 改造前行为：每次移动都生成 */
    return (g->moves % (uint32_t)g->config.spawn_interval) == 0;
}
int gy_selftest(void)
{
    /* Boot-only workspace in BSS, not on the small RT-Thread main stack. */
    static GyGame g, copy, sim;
    static GyPreview p;
    GyMove m = {GY_UP,1};
    if (!gy_init(&g,gy_default_config(),1)) return 0;
    copy = g;
    if (!gy_preview(&g,m,&p) || memcmp(&g,&copy,sizeof(g))) return 0;
    if (!gy_simulate_move(&g,m,&sim,1) || memcmp(&sim,&p.after,sizeof(sim))) return 0;
    if (!gy_simulate_move(&g,m,&sim,0) ||
        sim.turn != (g.turn ^ 1) || sim.rng != g.rng ||
        memcmp(sim.cells,p.visible.cells,sizeof(sim.cells)) != 0) return 0;
    if (!gy_commit(&g,&p) || gy_commit(&g,&p)) return 0;
    return g.revision == 1;
}
