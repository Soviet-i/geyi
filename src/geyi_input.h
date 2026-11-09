#ifndef GEYI_INPUT_H
#define GEYI_INPUT_H
#include "geyi_game.h"

/* 倾斜切换缓冲阈值（百分比）的取值范围与默认值。
 * 默认 10 与改造前的硬编码一致（见 docs/02、docs/adr/0003）；参数化后由 geyi_config 提供实际值。 */
#define GY_INPUT_BUFFER_MIN      0
#define GY_INPUT_BUFFER_MAX      50
#define GY_INPUT_BUFFER_DEFAULT  10

/* 幅度门限（百分之一度）：6° 进入预览、4° 才退出。
 * 两者之间的 2° 回差用于吸收手抖，避免在门限附近反复 CANCEL（docs/07 §9）。 */
#define GY_INPUT_ENTER_MAG       600
#define GY_INPUT_EXIT_MAG        400
/* 数量档位：从进入门限起每 10° 一档；N 档放不下时退回 (MAX-ENTER)/N。 */
#define GY_INPUT_BAND_MAG        1000
#define GY_INPUT_MAX_MAG         6000
/* 方向/数量变化后保持一致的稳定窗口。 */
#define GY_INPUT_STABLE_MS       200
/* 放兵列映射：水平 ±20° 覆盖底线全宽，±4° 死区保持当前列。 */
#define GY_INPUT_PLACE_FULL      2000
#define GY_INPUT_PLACE_DEAD      400

/* GY_INPUT_SELECT：从无到有出现可见选择。
 * GY_INPUT_UPDATE：可见选择的方向或数量变化（调用者应重新 preview，不要先清空）。
 * GY_INPUT_CANCEL：回中/失效，可见选择消失。
 * GY_INPUT_CONFIRM：确认一次；之后锁定到回中（低于 4°）。 */
enum { GY_INPUT_SELECT=1, GY_INPUT_CANCEL=2, GY_INPUT_CONFIRM=4, GY_INPUT_UPDATE=8 };
typedef struct {
    int direction, count, available, pending_direction, pending_count, pending_available;
    int raw_key, stable_key, armed, started;
    /* 确认后置 1，幅度低于 GY_INPUT_EXIT_MAG 才清零；启动/重置不置位，
     * 保证“轮到自己时板子已经斜着”也能进入预览。 */
    int locked;
    /* 两处滞回阈值（百分比）：改造前硬编码为 10%，现由配置提供。
     * 0 表示无滞回；见 gy_input_configure()。 */
    int dir_buffer_pct, angle_buffer_pct;
    uint32_t key_since, pending_since, last_ms;
} GyInput;
typedef struct { int events, direction, count; } GyInputEvent;
void gy_input_reset(GyInput *input);
/* 设置两处缓冲阈值（百分比），越界钳到 [GY_INPUT_BUFFER_MIN, GY_INPUT_BUFFER_MAX]。
 * 与 gy_input_reset() 配合使用：reset 先置默认 10，随后按配置覆盖。 */
void gy_input_configure(GyInput *input, int dir_pct, int angle_pct);
/* Screen coordinates: positive x=right, positive y=down; angle unit=0.01 degree.
 * Samples must be chronological; uint32 wrap supported, not arbitrary time travel.
 * available[4] comes from the CURRENT authoritative board, not a stale snapshot.
 */
GyInputEvent gy_input_update(GyInput *input, int x, int y, int key_down,
                             uint32_t now_ms, const int available[4]);
/* 放置模式：把倾斜向量映射到 owner 的己方底线列，返回最近的可放置空列（0..size-1）。
 * current=-1 表示当前没有高亮列；目标列被占用时由近到远找空列；没有空位返回 -1。
 * 只读 game，不修改任何状态，便于主机测试。 */
int gy_input_place_column(const GyGame *game, int owner, int x, int y, int current);
int gy_input_selftest(void);
#endif
