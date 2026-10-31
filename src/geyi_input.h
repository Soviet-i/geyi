#ifndef GEYI_INPUT_H
#define GEYI_INPUT_H
#include "geyi_game.h"

/* 倾斜切换缓冲阈值（百分比）的取值范围与默认值。
 * 默认 10 与改造前的硬编码一致（见 docs/02、docs/adr/0003）；参数化后由 geyi_config 提供实际值。 */
#define GY_INPUT_BUFFER_MIN      0
#define GY_INPUT_BUFFER_MAX      50
#define GY_INPUT_BUFFER_DEFAULT  10

enum { GY_INPUT_SELECT=1, GY_INPUT_CANCEL=2, GY_INPUT_CONFIRM=4 };
typedef struct {
    int direction, count, available, pending_direction, pending_count;
    int raw_key, stable_key, armed, latched, started;
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
int gy_input_selftest(void);
#endif
