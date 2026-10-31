#include "geyi_ai.h"
#include <string.h>

#define GY_AI_MAX_DEPTH 5
#define GY_AI_MAX_ACTIONS (4 * GY_MAX * GY_MAX + GY_MAX)
#define GY_AI_INF 1000000
#define GY_AI_WIN 100000

#define AI_W_SOLDIER 500
#define AI_W_HP 20
#define AI_W_COIN 100
#define AI_W_THREAT 10
#define AI_W_SLOT 30
#define AI_W_MOBILITY 10

typedef struct {
    uint8_t kind, direction, count, column;
    int16_t order;
} AiMove;

typedef struct {
    int soldiers, hp, coins, slots, damage;
} SideEval;

typedef struct {
    const GyAiConfig *cfg;
    GyAiStats stats;
    uint32_t start_ms;
    int use_time;
    int aborted;
    int completed_depth;
    int have_best;
    AiMove best;
    GyGame nodes[GY_AI_MAX_DEPTH + 1];
    AiMove actions[GY_AI_MAX_DEPTH + 1][GY_AI_MAX_ACTIONS];
    int action_count[GY_AI_MAX_DEPTH + 1];
} GyAiSearch;

static const int ai_dx[4] = {0, 1, 0, -1};
static const int ai_dy[4] = {-1, 0, 1, 0};

static int ai_inside(const GyGame *g, int x, int y)
{
    return x >= 0 && y >= 0 && x < g->config.size && y < g->config.size;
}

static void ai_material(const GyGame *g, int owner, int *soldiers, int *hp)
{
    int x, y, n = 0, h = 0;
    for (y = 0; y < g->config.size; ++y) for (x = 0; x < g->config.size; ++x) {
        const GyCell *c = &g->cells[y][x];
        if (c->kind == GY_SOLDIER && c->owner == owner) {
            ++n;
            if (c->hp > 0) h += c->hp;
        }
    }
    *soldiers = n; *hp = h;
}

/* Damage a number could deal to owner's soldiers if the matching direction
 * were chosen. The first non-empty cell in each direction is considered;
 * numbers themselves block the line conservatively. */
static int ai_damage_to(const GyGame *g, int owner)
{
    int x, y, d, total = 0;
    for (y = 0; y < g->config.size; ++y) for (x = 0; x < g->config.size; ++x) {
        const GyCell *c = &g->cells[y][x];
        if (c->kind != GY_NUMBER) continue;
        for (d = 0; d < 4; ++d) {
            int nx = x + ai_dx[d], ny = y + ai_dy[d];
            while (ai_inside(g, nx, ny) && g->cells[ny][nx].kind == GY_EMPTY) {
                nx += ai_dx[d]; ny += ai_dy[d];
            }
            if (ai_inside(g, nx, ny)) {
                const GyCell *t = &g->cells[ny][nx];
                if (t->kind == GY_SOLDIER && t->owner == owner && t->hp > 0) {
                    int value = (int)c->value;
                    total += value < t->hp ? value : t->hp;
                }
            }
        }
    }
    return total;
}

static void ai_eval_side(const GyGame *g, int owner, SideEval *e)
{
    int x, row, cap;
    memset(e, 0, sizeof(*e));
    for (row = 0; row < g->config.size; ++row) for (x = 0; x < g->config.size; ++x) {
        const GyCell *c = &g->cells[row][x];
        if (c->kind == GY_SOLDIER && c->owner == owner) {
            ++e->soldiers;
            if (c->hp > 0) e->hp += c->hp;
        }
    }
    row = owner == 0 ? g->config.size - 1 : 0;
    for (x = 0; x < g->config.size; ++x)
        if (g->cells[row][x].kind == GY_EMPTY) ++e->slots;
    e->coins = g->coins[owner];
    cap = g->config.cost * (e->slots + 1);
    if (e->coins > cap) e->coins = cap;
    e->damage = ai_damage_to(g, owner);
}

static int ai_terminal_score(const GyGame *g)
{
    if (g->outcome == GY_DRAW) return 0;
    if (g->outcome == GY_RED_WIN) return g->turn == 0 ? GY_AI_WIN : -GY_AI_WIN;
    if (g->outcome == GY_BLUE_WIN) return g->turn == 1 ? GY_AI_WIN : -GY_AI_WIN;
    return 0;
}

static int ai_evaluate(const GyGame *g)
{
    SideEval me, them;
    int d, mobility = 0, score;
    if (g->outcome != GY_PLAYING) return ai_terminal_score(g);
    ai_eval_side(g, g->turn, &me);
    ai_eval_side(g, 1 - g->turn, &them);
    score = AI_W_SOLDIER * (me.soldiers - them.soldiers);
    score += AI_W_HP * (me.hp - them.hp);
    score += AI_W_COIN * (me.coins - them.coins);
    score += AI_W_THREAT * (them.damage - me.damage);
    score += AI_W_SLOT * (me.slots - them.slots);
    for (d = 0; d < 4; ++d) mobility += gy_eligible_count(g, d);
    if (g->coins[g->turn] >= g->config.cost) mobility += me.slots;
    score += AI_W_MOBILITY * mobility;
    if (score > GY_AI_WIN - 1) score = GY_AI_WIN - 1;
    if (score < -(GY_AI_WIN - 1)) score = -(GY_AI_WIN - 1);
    return score;
}

/* Move-ordering score only; do not add these event terms to the leaf eval. */
static int ai_order_score(const GyGame *g, const GyGame *after)
{
    int own = g->turn, enemy = 1 - own;
    int o0, h0, e0, eh0, o1, h1, e1, eh1, score;
    ai_material(g, own, &o0, &h0);
    ai_material(g, enemy, &e0, &eh0);
    ai_material(after, own, &o1, &h1);
    ai_material(after, enemy, &e1, &eh1);
    score = (o1 - o0) * AI_W_SOLDIER + (h1 - h0) * AI_W_HP;
    score += (e0 - e1) * AI_W_SOLDIER + (eh0 - eh1) * AI_W_HP;
    score += (after->coins[own] - g->coins[own]) * AI_W_COIN;
    score += (g->coins[enemy] - after->coins[enemy]) * AI_W_COIN;
    if (score > 30000) score = 30000;
    if (score < -30000) score = -30000;
    return score;
}

static int ai_generate(const GyGame *g, AiMove *out)
{
    int n = 0, d, count, avail, i, row;
    GyGame tmp;
    for (d = 0; d < 4; ++d) {
        avail = gy_eligible_count(g, d);
        if (avail > 0) {
            for (count = 1; count <= avail && n < GY_AI_MAX_ACTIONS; ++count, ++n) {
                out[n].kind = GY_AI_MOVE; out[n].direction = (uint8_t)d;
                out[n].count = (uint8_t)count; out[n].column = 0; out[n].order = 0;
            }
        } else if (n < GY_AI_MAX_ACTIONS) {
            out[n].kind = GY_AI_MOVE; out[n].direction = (uint8_t)d;
            out[n].count = 0; out[n].column = 0; out[n].order = 0;
            ++n;
        }
    }
    if (g->coins[g->turn] >= g->config.cost) {
        row = g->turn == 0 ? g->config.size - 1 : 0;
        for (i = 0; i < g->config.size && n < GY_AI_MAX_ACTIONS; ++i) {
            if (g->cells[row][i].kind == GY_EMPTY) {
                out[n].kind = GY_AI_PLACE; out[n].direction = 0;
                out[n].count = 0; out[n].column = (uint8_t)i; out[n].order = 0;
                ++n;
            }
        }
    }
    {
        int m = 0;
        for (i = 0; i < n; ++i) {
            int ok;
            if (out[i].kind == GY_AI_MOVE) {
                GyMove mv;
                mv.direction = out[i].direction; mv.count = out[i].count;
                ok = gy_simulate_move(g, mv, &tmp, 0);
            } else {
                tmp = *g;
                ok = gy_place(&tmp, out[i].column);
            }
            if (ok) {
                out[m] = out[i];
                out[m].order = (int16_t)ai_order_score(g, &tmp);
                ++m;
            }
        }
        n = m;
    }
    return n;
}

static void ai_sort(AiMove *a, int n)
{
    int i, j;
    AiMove x;
    for (i = 1; i < n; ++i) {
        x = a[i];
        j = i - 1;
        while (j >= 0 && a[j].order < x.order) { a[j + 1] = a[j]; --j; }
        a[j + 1] = x;
    }
}

static void ai_promote_hint(AiMove *a, int n, const AiMove *hint)
{
    int i, j;
    if (!hint->kind) return;
    for (i = 0; i < n; ++i) {
        if (a[i].kind == hint->kind && a[i].direction == hint->direction &&
            a[i].count == hint->count && a[i].column == hint->column) {
            AiMove x = a[i];
            for (j = i; j > 0; --j) a[j] = a[j - 1];
            a[0] = x;
            return;
        }
    }
}

static int ai_negamax(GyAiSearch *s, const GyGame *g, int depth,
                      int alpha, int beta, int ply, AiMove *best)
{
    int n, i, best_score;
    if (s->cfg->cancel && *s->cfg->cancel) { s->aborted = 1; return 0; }
    ++s->stats.nodes;
    if (s->cfg->node_limit && s->stats.nodes >= s->cfg->node_limit) {
        s->aborted = 1; return 0;
    }
    if (s->use_time && (s->stats.nodes & 255u) == 0u) {
        uint32_t now = s->cfg->now_ms();
        if ((uint32_t)(now - s->start_ms) >= s->cfg->time_limit_ms) {
            s->aborted = 1; return 0;
        }
    }
    if (g->outcome != GY_PLAYING) return ai_terminal_score(g);
    if (depth <= 0) return ai_evaluate(g);
    n = ai_generate(g, s->actions[ply]);
    s->action_count[ply] = n;
    if (n <= 0) return ai_evaluate(g);
    ai_sort(s->actions[ply], n);
    if (ply == 0 && s->have_best) ai_promote_hint(s->actions[ply], n, &s->best);
    best_score = -GY_AI_INF;
    for (i = 0; i < n; ++i) {
        AiMove m = s->actions[ply][i];
        int ok;
        if (m.kind == GY_AI_MOVE) {
            GyMove mv;
            mv.direction = m.direction; mv.count = m.count;
            ok = gy_simulate_move(g, mv, &s->nodes[ply], 0);
        } else {
            s->nodes[ply] = *g;
            ok = gy_place(&s->nodes[ply], m.column);
        }
        if (!ok) continue;
        /* A terminal child keeps the mover's turn (gy_preview semantics), so
         * its score is already from this node's perspective; do not negate. */
        if (s->cfg->use_pruning) {
            int score = s->nodes[ply].outcome != GY_PLAYING
                ? ai_terminal_score(&s->nodes[ply])
                : -ai_negamax(s, &s->nodes[ply], depth - 1,
                              -beta, -alpha, ply + 1, NULL);
            if (s->aborted) return 0;
            if (score > best_score) {
                best_score = score;
                if (ply == 0 && best) *best = m;
                if (score > alpha) alpha = score;
            }
            if (alpha >= beta) { ++s->stats.cutoffs; break; }
        } else {
            int score = s->nodes[ply].outcome != GY_PLAYING
                ? ai_terminal_score(&s->nodes[ply])
                : -ai_negamax(s, &s->nodes[ply], depth - 1,
                              -GY_AI_INF, GY_AI_INF, ply + 1, NULL);
            if (s->aborted) return 0;
            if (score > best_score) {
                best_score = score;
                if (ply == 0 && best) *best = m;
            }
        }
    }
    if (best_score == -GY_AI_INF) return ai_evaluate(g);
    return best_score;
}

GyAiConfig gy_ai_default_config(void)
{
    GyAiConfig c = {5, 1, 1, 300, 200000, NULL, NULL};
    return c;
}

int gy_ai_choose(const GyGame *g, const GyAiConfig *cfg,
                 GyAiAction *out, GyAiStats *stats)
{
    static GyAiSearch s;
    static volatile int busy;
    GyAiConfig c;
    AiMove best;
    int best_score = -GY_AI_INF;
    int depth;
    if (!g || !out || busy) return 0;
    memset(out, 0, sizeof(*out));
    if (g->outcome != GY_PLAYING) return 0;
    c = cfg ? *cfg : gy_ai_default_config();
    if (c.max_depth < 1) c.max_depth = 1;
    if (c.max_depth > GY_AI_MAX_DEPTH) c.max_depth = GY_AI_MAX_DEPTH;
    memset(&s, 0, sizeof(s));
    memset(&best, 0, sizeof(best));
    s.cfg = &c;
    s.start_ms = c.now_ms ? c.now_ms() : 0;
    s.use_time = c.now_ms != NULL && c.time_limit_ms > 0;
    busy = 1;
    if (c.use_iterative) {
        for (depth = 1; depth <= c.max_depth; ++depth) {
            AiMove iter;
            int score;
            memset(&iter, 0, sizeof(iter));
            s.aborted = 0;
            score = ai_negamax(&s, g, depth, -GY_AI_INF, GY_AI_INF, 0, &iter);
            if (s.aborted) break;
            if (!iter.kind) break;
            best = iter; best_score = score;
            s.best = iter; s.have_best = 1; s.completed_depth = depth;
            if (score >= GY_AI_WIN - 1) break;
        }
    } else {
        AiMove iter;
        int score;
        memset(&iter, 0, sizeof(iter));
        s.aborted = 0;
        score = ai_negamax(&s, g, c.max_depth, -GY_AI_INF, GY_AI_INF, 0, &iter);
        if (iter.kind) {
            best = iter; best_score = score;
            if (!s.aborted) s.completed_depth = c.max_depth;
        }
    }
    busy = 0;
    s.stats.depth = s.completed_depth;
    if (c.now_ms) s.stats.elapsed_ms = (uint32_t)(c.now_ms() - s.start_ms);
    if (stats) *stats = s.stats;
    if (!best.kind) return 0;
    out->kind = best.kind;
    out->move.direction = best.direction;
    out->move.count = best.count;
    out->column = best.column;
    out->score = best_score;
    return 1;
}

int gy_ai_selftest(void)
{
    GyGame g, after;
    GyAiConfig c;
    GyAiAction a;
    GyAiStats stats;
    GyMove mv;
    GyCell red = {GY_SOLDIER, 0, 8, 0};
    GyCell blue = {GY_SOLDIER, 1, 1, 0};
    if (!gy_init(&g, gy_default_config(), 1)) return 0;
    memset(g.cells, 0, sizeof(g.cells));
    g.turn = 0; g.outcome = GY_PLAYING;
    g.coins[0] = g.coins[1] = 0;
    g.moves = 0; g.revision = 0; g.rng = 1;
    g.cells[0][0] = red;
    g.cells[0][2] = red;
    g.cells[1][0] = blue;
    c = gy_ai_default_config();
    c.max_depth = 2; c.time_limit_ms = 0; c.node_limit = 0;
    c.now_ms = NULL; c.cancel = NULL;
    if (!gy_ai_choose(&g, &c, &a, &stats)) return 0;
    if (a.kind != GY_AI_MOVE) return 0;
    mv.direction = a.move.direction;
    mv.count = a.move.count;
    if (!gy_simulate_move(&g, mv, &after, 0)) return 0;
    return after.outcome == GY_RED_WIN;
}
