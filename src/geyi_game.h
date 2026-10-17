#ifndef GEYI_GAME_H
#define GEYI_GAME_H
#include <stdint.h>
#define GY_MAX 8
enum { GY_EMPTY, GY_SOLDIER, GY_NUMBER };
enum { GY_UP, GY_RIGHT, GY_DOWN, GY_LEFT };
enum { GY_PLAYING, GY_RED_WIN, GY_BLUE_WIN, GY_DRAW };
typedef struct { int kind, owner, hp; uint32_t value; } GyCell;
typedef struct {
    int size, hp, attack, cost, merge_reward, kill_reward;
} GyConfig;
typedef struct {
    GyConfig config;
    GyCell cells[GY_MAX][GY_MAX];
    int turn, outcome, coins[2];
    uint32_t revision, moves, rng;
} GyGame;
typedef struct { int direction, count; } GyMove;
typedef struct { GyGame before, after, visible; int valid; } GyPreview;
GyConfig gy_default_config(void);
int gy_init(GyGame *g, GyConfig config, uint32_t seed);
int gy_preview(const GyGame *g, GyMove move, GyPreview *preview);
/* Apply a move to a copy. spawn_next=0 is the AI-search state: no random
 * tile is generated, but the turn is toggled. spawn_next=1 matches
 * gy_preview's after state. next must not alias g. */
int gy_simulate_move(const GyGame *g, GyMove move, GyGame *next, int spawn_next);
int gy_eligible_count(const GyGame *g, int direction);
int gy_commit(GyGame *g, const GyPreview *preview);
int gy_place(GyGame *g, int column);
int gy_resign(GyGame *g);
int gy_selftest(void);
#endif
