#ifndef GEYI_BOARD_H
#define GEYI_BOARD_H
#ifdef GEYI_WITH_LVGL
int gy_board_start(void);
void gy_board_poll(void);
/* 串口自动化注入：只替换板端采样源，仍走 gy_board_poll 的同一套
 * 新鲜度/失效/KEY2 防抖/倾斜状态机，不直接调用 UI。 */
void gy_board_test_tilt(int x,int y);
void gy_board_test_key(void);
void gy_board_test_off(void);
#endif
#endif
