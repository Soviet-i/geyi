#ifndef GEYI_FEEDBACK_H
#define GEYI_FEEDBACK_H
#include <stdint.h>
enum { GY_FX_NONE, GY_FX_CLEAR, GY_FX_MOVE, GY_FX_RED, GY_FX_BLUE, GY_FX_DRAW };
typedef struct { uint32_t started; int kind; } GyLight;
void gy_light_start(GyLight *light,int event,uint32_t now);
uint32_t gy_light_color(const GyLight *light,uint32_t now);
int gy_light_selftest(void);
#endif
