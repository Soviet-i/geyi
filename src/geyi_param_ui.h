/*
 * geyi_param_ui.h —— 参数页的纯逻辑状态（REQ-403/404/408）
 *
 * 只维护“当前模式、当前页、条目映射、增减/重置”这些可测试状态，
 * 不依赖 LVGL。界面层负责把这里的结果画出来；主机测试直接覆盖本模块。
 */
#ifndef GEYI_PARAM_UI_H
#define GEYI_PARAM_UI_H

#include "geyi_config.h"

#define GY_PARAM_UI_ROWS 5

/* 初始化：单机模式、第 1 页、三类配置回到官方默认值。 */
void gy_param_ui_init(void);

int gy_param_ui_mode(void);
int gy_param_ui_set_mode(int mode);      /* 1=成功，0=模式非法 */

int gy_param_ui_page(void);
int gy_param_ui_page_count(void);
int gy_param_ui_page_next(void);         /* 1=页码改变 */
int gy_param_ui_page_prev(void);

/* 当前页第 row 行对应的参数 id；空行或当前模式不适用的参数返回 -1。 */
int gy_param_ui_item_at(int row);

int gy_param_ui_value(int id);
/* 在当前模式当前值上加减 delta；内部走 gy_param_set()，返回 GY_PARAM_* 错误码。
 * 越界仍会钳位写入，返回码如实反映原始输入。 */
int gy_param_ui_adjust(int id, int delta);
int gy_param_ui_reset(void);

/* 主机/板端自检：返回 0 表示通过。 */
int gy_param_ui_selftest(void);

#endif /* GEYI_PARAM_UI_H */
