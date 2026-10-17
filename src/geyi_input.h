#ifndef GEYI_INPUT_H
#define GEYI_INPUT_H
#include "geyi_game.h"
enum { GY_INPUT_SELECT=1, GY_INPUT_CANCEL=2, GY_INPUT_CONFIRM=4 };
typedef struct {
    int direction, count, available, pending_direction, pending_count;
    int raw_key, stable_key, armed, latched, started;
    uint32_t key_since, pending_since, last_ms;
} GyInput;
typedef struct { int events, direction, count; } GyInputEvent;
void gy_input_reset(GyInput *input);
/* Screen coordinates: positive x=right, positive y=down; angle unit=0.01 degree.
 * Samples must be chronological; uint32 wrap supported, not arbitrary time travel.
 * available[4] comes from the CURRENT authoritative board, not a stale snapshot.
 */
GyInputEvent gy_input_update(GyInput *input, int x, int y, int key_down,
                             uint32_t now_ms, const int available[4]);
int gy_input_selftest(void);
#endif
