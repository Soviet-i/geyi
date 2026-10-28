#ifndef GEYI_AI_TASK_H
#define GEYI_AI_TASK_H
#include "geyi_ai.h"

typedef struct {
    int ready;
    uint32_t revision;
    uint32_t generation;
    GyAiAction action;
    GyAiStats stats;
} GyAiResult;

/* Start one search on a snapshot. Returns 0 if started, -1 if busy/error.
 * On the board this runs in a dedicated RT-Thread; on the desktop host it
 * runs synchronously and the result is returned by the next poll. */
int gy_ai_task_start(const GyGame *g, const GyAiConfig *cfg,
                     uint32_t revision, uint32_t generation);
int gy_ai_task_poll(GyAiResult *out);
void gy_ai_task_cancel(void);
#endif
