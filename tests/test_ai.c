#include "geyi_ai.h"
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static GyGame empty(void)
{
    GyGame g;
    gy_init(&g, gy_default_config(), 7);
    memset(g.cells, 0, sizeof(g.cells));
    g.turn = 0; g.outcome = GY_PLAYING;
    g.coins[0] = g.coins[1] = 0;
    g.moves = 0; g.revision = 0; g.rng = 1;
    return g;
}
static void soldier(GyGame *g, int x, int y, int owner, int hp)
{
    GyCell c = {GY_SOLDIER, owner, hp, 0};
    g->cells[y][x] = c;
}
static void number(GyGame *g, int x, int y, uint32_t value)
{
    GyCell c = {GY_NUMBER, 0, 0, value};
    g->cells[y][x] = c;
}
static int total_hp(const GyGame *g, int owner)
{
    int x, y, total = 0;
    for (y = 0; y < g->config.size; ++y) for (x = 0; x < g->config.size; ++x)
        if (g->cells[y][x].kind == GY_SOLDIER && g->cells[y][x].owner == owner)
            total += g->cells[y][x].hp;
    return total;
}
static int apply_action(const GyGame *g, const GyAiAction *a, GyGame *after)
{
    if (a->kind == GY_AI_MOVE) return gy_simulate_move(g, a->move, after, 0);
    if (a->kind == GY_AI_PLACE) { *after = *g; return gy_place(after, a->column); }
    return 0;
}
static int same_action(const GyAiAction *a, const GyAiAction *b)
{
    return a->kind == b->kind && a->move.direction == b->move.direction &&
           a->move.count == b->move.count && a->column == b->column;
}
static GyAiConfig config(int depth, int pruning)
{
    GyAiConfig c = gy_ai_default_config();
    c.max_depth = depth;
    c.use_pruning = pruning;
    c.use_iterative = 0;
    c.time_limit_ms = 0;
    c.node_limit = 0;
    c.now_ms = NULL;
    c.cancel = NULL;
    return c;
}

int main(void)
{
    GyGame g, after;
    GyAiAction a, b;
    GyAiStats sa, sb;
    GyAiConfig c;

    CHECK(gy_ai_selftest());

    /* Immediate win: red can kill the only blue soldier. */
    g = empty();
    soldier(&g, 0, 0, 0, 8);
    soldier(&g, 2, 0, 0, 8);
    soldier(&g, 0, 1, 1, 1);
    c = config(2, 1);
    CHECK(gy_ai_choose(&g, &c, &a, &sa));
    CHECK(apply_action(&g, &a, &after));
    CHECK(after.outcome == GY_RED_WIN);

    /* Avoid an obvious self-damage: the up direction moves the 4 into our
     * own soldier; other directions are safe. */
    g = empty();
    soldier(&g, 0, 0, 0, 8);
    soldier(&g, 2, 0, 0, 8);
    number(&g, 0, 2, 4);
    soldier(&g, 5, 5, 1, 8);
    c = config(2, 1);
    CHECK(gy_ai_choose(&g, &c, &a, &sa));
    CHECK(apply_action(&g, &a, &after));
    CHECK(total_hp(&after, 0) == total_hp(&g, 0));

    /* Same board, different RNG state: the AI must not look at the random
     * stream while searching (spawn=0). */
    CHECK(gy_init(&g, gy_default_config(), 7));
    b = a;
    {
        GyGame h = g;
        h.rng = 0x12345678u;
        GyAiAction other;
        GyAiStats stats;
        c = config(2, 1);
        CHECK(gy_ai_choose(&g, &c, &a, &sa));
        CHECK(gy_ai_choose(&h, &c, &other, &stats));
        CHECK(same_action(&a, &other));
        {
            GyMove m = a.move;
            if (a.kind == GY_AI_MOVE) {
                CHECK(gy_simulate_move(&g, m, &after, 0));
                CHECK(after.rng == g.rng);
            }
        }
    }

    /* Alpha-beta must visit fewer nodes than full minimax at the same depth
     * and return the same root score. */
    CHECK(gy_init(&g, gy_default_config(), 20261015));
    c = config(3, 1);
    CHECK(gy_ai_choose(&g, &c, &a, &sa));
    c = config(3, 0);
    CHECK(gy_ai_choose(&g, &c, &b, &sb));
    CHECK(sa.nodes < sb.nodes);
    CHECK(sa.depth == 3 && sb.depth == 3);
    CHECK(a.score == b.score);

    /* Same state and config must produce the same decision. */
    c = config(2, 1);
    CHECK(gy_ai_choose(&g, &c, &a, &sa));
    CHECK(gy_ai_choose(&g, &c, &b, &sb));
    CHECK(same_action(&a, &b));
    CHECK(a.score == b.score);

    /* Short AI-vs-AI self-play through the real spawn/commit path. */
    {
        GyGame sim;
        int ply;
        CHECK(gy_init(&sim, gy_default_config(), 42));
        c = config(2, 1);
        for (ply = 0; ply < 30 && sim.outcome == GY_PLAYING; ++ply) {
            CHECK(gy_ai_choose(&sim, &c, &a, &sa));
            if (a.kind == GY_AI_MOVE) {
                GyPreview p;
                CHECK(gy_preview(&sim, a.move, &p));
                CHECK(gy_commit(&sim, &p));
            } else {
                CHECK(gy_place(&sim, a.column));
            }
        }
        CHECK(ply > 0);
    }

    /* Cancellation must return without a move. */
    {
        volatile int cancel = 1;
        c = config(5, 1);
        c.cancel = &cancel;
        CHECK(!gy_ai_choose(&g, &c, &a, &sa));
    }

    printf("PASS: %d AI assertions (legal win, self-damage, RNG independence, pruning, cancel)\n", checks);
    return 0;
}
