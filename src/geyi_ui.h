#ifndef GEYI_UI_H
#define GEYI_UI_H
#ifdef GEYI_WITH_LVGL
#include "lvgl.h"
#include "geyi_game.h"
enum { GY_UI_UP, GY_UI_RIGHT, GY_UI_DOWN, GY_UI_LEFT, GY_UI_LESS,
       GY_UI_MORE, GY_UI_CONFIRM, GY_UI_CANCEL, GY_UI_PLACE, GY_UI_RESTART, GY_UI_RESIGN };
void gy_ui_create(void);
void gy_ui_action(int action);
void gy_ui_cell(int column,int row);
const GyGame *gy_ui_game(void);
int gy_ui_has_preview(void);
int gy_ui_is_placing(void);
void gy_ui_tilt_select(int direction, int count);
#endif
#endif
