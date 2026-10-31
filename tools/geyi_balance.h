#ifndef GEYI_BALANCE_H
#define GEYI_BALANCE_H

#include <stddef.h>
#include "geyi_game.h"

/* 固定参数网格的组数（REQ-411/TC-408）。 */
int gy_balance_group_count(void);

/* 运行前 group_count 组、每组 games_per_group 局，输出 CSV 到 out。
 * 同一组参数/种子集合结果可复现；返回 0 成功，-1 缓冲区不足或参数非法。 */
int gy_balance_scan_csv(int group_count, int games_per_group,
                        char *out, size_t out_size);

#endif
