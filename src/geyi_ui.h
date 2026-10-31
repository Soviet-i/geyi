#ifndef GEYI_UI_H
#define GEYI_UI_H
#ifdef GEYI_WITH_LVGL
#include "lvgl.h"
#include "geyi_game.h"
#include "geyi_feedback.h"
enum { GY_UI_UP, GY_UI_RIGHT, GY_UI_DOWN, GY_UI_LEFT, GY_UI_LESS,
       GY_UI_MORE, GY_UI_CONFIRM, GY_UI_CANCEL, GY_UI_PLACE, GY_UI_RESTART, GY_UI_RESIGN,
       GY_UI_HOME, GY_UI_MODES, GY_UI_SINGLE, GY_UI_AI,
       GY_UI_PREP_SWAP, GY_UI_PREP_CANCEL, GY_UI_PREP_START,
       GY_UI_PARAMS, GY_UI_PARAM_BACK, GY_UI_PARAM_PREV, GY_UI_PARAM_NEXT,
       GY_UI_PARAM_RESET, GY_UI_PARAM_MODE_SINGLE, GY_UI_PARAM_MODE_PVE,
       GY_UI_PARAM_MODE_PVP };
void gy_ui_create(void);
void gy_ui_action(int action);
void gy_ui_cell(int column,int row);
void gy_ui_tick(void);
const GyGame *gy_ui_game(void);
int gy_ui_has_preview(void);
int gy_ui_is_placing(void);
int gy_ui_is_playing(void);
int gy_ui_take_feedback(void);
int gy_ui_ai_mode(void);
int gy_ui_ai_thinking(void);
int gy_ui_ai_previewing(void);
int gy_ui_prep_active(void);
int gy_ui_prep_human_red(void);
int gy_ui_param_active(void);
void gy_ui_tilt_select(int direction, int count);
#endif
#endif
