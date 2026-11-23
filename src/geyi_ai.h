#ifndef GEYI_AI_H
#define GEYI_AI_H
#include "geyi_game.h"

enum { GY_AI_MOVE = 1, GY_AI_PLACE = 2 };

typedef struct {
    int kind;       /* GY_AI_MOVE or GY_AI_PLACE */
    GyMove move;    /* valid when kind == GY_AI_MOVE */
    int column;     /* valid when kind == GY_AI_PLACE */
    int score;      /* root score from the AI side's perspective */
} GyAiAction;

typedef uint32_t (*GyAiNowMsFn)(void);

typedef struct {
    int max_depth;            /* clamped to 1..5 */
    int use_pruning;          /* 1 = alpha-beta, 0 = full minimax for comparison */
    int use_iterative;        /* 1 = iterative deepening, 0 = only max_depth */
    uint32_t time_limit_ms;   /* 0 = no time limit */
    uint32_t node_limit;      /* 0 = no node limit */
    GyAiNowMsFn now_ms;       /* NULL disables time checks */
    const volatile int *cancel; /* optional cancellation flag */
} GyAiConfig;

typedef struct {
    uint64_t nodes;
    uint64_t cutoffs;
    uint32_t elapsed_ms;
    int depth;                /* last completed depth */
} GyAiStats;

GyAiConfig gy_ai_default_config(void);
/* Not reentrant: call from one AI worker at a time. Returns 0 if the game is
 * over, no legal action exists, or the search was cancelled before a move. */
int gy_ai_choose(const GyGame *g, const GyAiConfig *cfg,
                 GyAiAction *out, GyAiStats *stats);
int gy_ai_selftest(void);
#endif
