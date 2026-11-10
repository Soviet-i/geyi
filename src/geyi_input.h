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
/* 回中退出宽限：低于 4° 持续这么久才清除，避免第二局/触摸卡顿在门限附近闪断。 */
#define GY_INPUT_EXIT_HOLD_MS    120
/* 放兵落点：外围边/角按方位映射，±4° 死区保持当前目标。 */
#define GY_INPUT_PLACE_DEAD      400

/* GY_INPUT_SELECT：从无到有出现可见选择。
 * GY_INPUT_UPDATE：可见选择的方向或数量变化（调用者应重新 preview，不要先清空）。
 * GY_INPUT_CANCEL：回中/失效，可见选择消失。
 * GY_INPUT_CONFIRM：确认一次；之后锁定到回中（低于 4°）。 */
enum { GY_INPUT_SELECT=1, GY_INPUT_CANCEL=2, GY_INPUT_CONFIRM=4, GY_INPUT_UPDATE=8 };
typedef struct {
    int direction, count, available, pending_direction, pending_count, pending_available;
    int aim;                    /* -100..100 横向方位比例 */
    uint32_t aim_since;         /* aim-only 更新的稳定起点 */
    int raw_key, stable_key, armed, started;
    /* 确认后置 1，幅度低于 GY_INPUT_EXIT_MAG 才清零；启动/重置不置位，
     * 保证“轮到自己时板子已经斜着”也能进入预览。 */
    int locked;
    /* 两处滞回阈值（百分比）：改造前硬编码为 10%，现由配置提供。
     * 0 表示无滞回；见 gy_input_configure()。 */
    int dir_buffer_pct, angle_buffer_pct;
    uint32_t key_since, pending_since, exit_since;
} GyInput;
typedef struct { int events, direction, count, aim; } GyInputEvent;
typedef struct { int row, column; } GyInputPlace;
void gy_input_reset(GyInput *input);
/* 设置两处缓冲阈值（百分比），越界钳到 [GY_INPUT_BUFFER_MIN, GY_INPUT_BUFFER_MAX]。
 * 与 gy_input_reset() 配合使用：reset 先置默认 10，随后按配置覆盖。 */
void gy_input_configure(GyInput *input, int dir_pct, int angle_pct);
/* 页面/新局代次变化时重置输入；返回 1 表示发生了重置。 */
int gy_input_begin_session(GyInput *input, uint32_t session, uint32_t *last_session);
/* 倾斜向量 -> 四方向 / 横向比例。方向由主轴决定；aim=横向分量/主轴幅度*100。
 * 返回 -1 表示向量为 0。 */
int gy_input_direction(int x, int y);
int gy_input_aim(int x, int y, int direction);
/* Screen coordinates: positive x=right, positive y=down; angle unit=0.01 degree.
 * Samples must be chronological; uint32 wrap supported, not arbitrary time travel.
 * soldiers[4] comes from the CURRENT authoritative board; legal_mask bit d 表示该方向
 * 是否有有效走法（有士兵可动，或仅数字可滑动/合并）。士兵数为 0 但方向合法时，
 * 选择仍会出现，count=0。 */
GyInputEvent gy_input_update(GyInput *input, int x, int y, int key_down,
                             uint32_t now_ms, const int soldiers[4],
                             unsigned legal_mask);
/* 放置模式：按倾斜方向和方位比例从外侧边向内搜索第一个空格。
 * current_row/current_column 为 -1 表示没有当前目标；死区内保持当前目标。
 * 返回 1 并写 out；没有可用格返回 0。只读 game。 */
int gy_input_place_cell(const GyGame *game, int x, int y,
                        int current_row, int current_column, GyInputPlace *out);
int gy_input_selftest(void);
#endif
