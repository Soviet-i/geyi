/*
 * test_balance.c —— REQ-411/TC-408 平衡扫描主机测试
 *
 * 只验证“固定参数网格 + 固定种子集合”的可复现性和 CSV 结构；
 * 用小子集（2 组 × 3 局）避免测试耗时。
 */
#include "geyi_balance.h"
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL line %d: %s\n",__LINE__,#x); return 1;} } while(0)

int main(void)
{
    char a[16384], b[16384];
    CHECK(gy_balance_group_count() == 20);
    CHECK(gy_balance_scan_csv(2, 3, a, sizeof(a)) == 0);
    CHECK(gy_balance_scan_csv(2, 3, b, sizeof(b)) == 0);
    CHECK(strcmp(a, b) == 0);                         /* 同一网格/种子逐字节一致 */
    CHECK(strncmp(a, "group,size,spawn_interval,hp,attack,cost,merge_reward,kill_reward,games,red_wins,blue_wins,draws,avg_moves\n", 99) == 0);
    CHECK(strstr(a, "\n0,4,1,8,2,4,1,2,3,") != NULL);
    CHECK(strstr(a, "\n1,4,3,8,2,4,1,2,3,") != NULL);
    CHECK(gy_balance_scan_csv(0, 3, a, sizeof(a)) == -1);
    CHECK(gy_balance_scan_csv(21, 3, a, sizeof(a)) == -1);
    CHECK(gy_balance_scan_csv(2, 0, a, sizeof(a)) == -1);
    printf("PASS: %d assertions; TC-408 balance scan reproducibility\n", checks);
    return 0;
}
