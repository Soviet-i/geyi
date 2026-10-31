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
    /* 多少次"有效移动"生成一个数字单位；1 表示每次（改造前的行为）。
     * 由 geyi_config 参数模块提供；放置士兵不计数。见 docs/02 与 docs/adr/0003。 */
    int spawn_interval;
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
/* 本次有效移动后是否应生成数字单位。spawn_interval<=1 表示每次生成（改造前行为）。
 * 抽成独立函数是为了能在主机端测试，避免"参数是否生效"只能上板验证。 */
int gy_should_spawn(const GyGame *g);
int gy_selftest(void);
#endif
