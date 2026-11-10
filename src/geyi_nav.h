#ifndef GEYI_NAV_H
#define GEYI_NAV_H

/* 倾斜预览与屏幕按钮预览的来源仲裁（REQ-319）。
 * 纯逻辑、不依赖 LVGL，便于主机用例覆盖；gy_ui.c 只负责把状态映射到界面。 */
typedef struct { int tilt, button; } GyNav;

void gy_nav_reset(GyNav *nav);
/* 按钮预览期间倾斜应被忽略。 */
int gy_nav_tilt_allowed(const GyNav *nav);
/* 记录一次“倾斜生成了预览”；被按钮预览挡住时返回 0。 */
int gy_nav_tilt_selected(GyNav *nav);
/* 记录一次“屏幕方向/增减生成了预览”，并清掉倾斜来源。 */
int gy_nav_button_selected(GyNav *nav);
/* 板子回中：只取消倾斜预览，不打断按钮预览；取消成功返回 1。 */
int gy_nav_tilt_cancel(GyNav *nav);
/* 确认/取消/重开/切页：清空两个来源。 */
int gy_nav_clear(GyNav *nav);
/* 倾斜预览中，屏幕的取消/增减/方向/放兵属于编辑类动作，应被忽略。 */
int gy_nav_blocks_screen_edit(const GyNav *nav, int placing);
int gy_nav_tilt_active(const GyNav *nav);
int gy_nav_button_active(const GyNav *nav);
#endif
