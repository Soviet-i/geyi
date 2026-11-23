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
       GY_UI_PARAM_MODE_PVP,
       GY_UI_NET, GY_UI_NET_CREATE, GY_UI_NET_BACK,
       GY_UI_NET_JOIN0, GY_UI_NET_JOIN1, GY_UI_NET_JOIN2, GY_UI_NET_JOIN3,
       GY_UI_NET_JOIN4, GY_UI_NET_READY, GY_UI_NET_SWAP, GY_UI_NET_LEAVE,
       GY_UI_NET_CONFIRM_YES, GY_UI_NET_CONFIRM_NO,
       GY_UI_RULES, GY_UI_RULES_BACK, GY_UI_RULES_PREV, GY_UI_RULES_NEXT,
       GY_UI_HISTORY, GY_UI_HISTORY_BACK, GY_UI_HISTORY_PREV, GY_UI_HISTORY_NEXT,
       GY_UI_HISTORY_CLEAR };
void gy_ui_create(void);
void gy_ui_action(int action);
void gy_ui_cell(int column,int row);
void gy_ui_tick(void);
const GyGame *gy_ui_game(void);
int gy_ui_rules_active(void);
int gy_ui_rules_page(void);
int gy_ui_rules_content_bottom(void);
/* KEY2 单键语义：预览确认落子；放兵有选列确认、无选列退出；空闲进入放兵。 */
void gy_ui_key2(void);
/* 真机屏幕为圆角矩形：统计按钮/文字框越出四角安全区的数量（0 = 通过）。 */
int gy_ui_corner_clip_count(int radius);
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
void gy_ui_tilt_select(int direction, int count, int aim);
/* 板子回中/输入失效：只取消倾斜预览，不打断按钮预览。 */
void gy_ui_tilt_cancel(void);
/* 板端喂倾斜事件前询问：按钮预览期间返回 0（REQ-319）。 */
int gy_ui_tilt_allowed(void);
int gy_ui_preview_direction(void);
int gy_ui_preview_count(void);
int gy_ui_preview_aim(void);
/* 页面/新局代次；板端发现变化即重置输入会话。 */
uint32_t gy_ui_session(void);
/* 板上自动化测试入口：由 shell 线程写入，主循环在 gy_ui_tick 中执行。 */
void gy_ui_test_request(const char *command);
#endif
#endif
