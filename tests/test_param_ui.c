/*
 * test_param_ui.c —— 参数页状态的主机端测试（TC-407 的软件部分，REQ-403/404/408）
 *
 * 运行：python tools/run_host.py
 * 不依赖 LVGL、RT-Thread 或开发板；板端只需确认实际屏幕/触摸操作。
 */
#include "geyi_param_ui.h"
#include "geyi_config.h"
#include "geyi_game.h"
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL line %d: %s\n",__LINE__,#x); return 1;} } while(0)

static int test_pages(void)
{
    gy_param_ui_init();
    CHECK(gy_param_ui_mode() == GY_MODE_SINGLE);
    CHECK(gy_param_ui_page() == 0);
    CHECK(gy_param_ui_page_count() == 2);
    CHECK(gy_param_ui_item_at(0) == GY_P_SIZE);
    CHECK(gy_param_ui_item_at(4) == GY_P_COST);
    CHECK(gy_param_ui_item_at(-1) == -1);
    CHECK(gy_param_ui_item_at(GY_PARAM_UI_ROWS) == -1);

    CHECK(gy_param_ui_page_next() == 1);
    CHECK(gy_param_ui_page() == 1);
    CHECK(gy_param_ui_item_at(0) == GY_P_MERGE_REWARD);
    CHECK(gy_param_ui_item_at(1) == GY_P_KILL_REWARD);
    CHECK(gy_param_ui_item_at(2) == -1);       /* 单机模式不显示搜索深度 */
    CHECK(gy_param_ui_item_at(3) == GY_P_DIR_BUFFER);
    CHECK(gy_param_ui_item_at(4) == GY_P_ANGLE_BUFFER);
    CHECK(gy_param_ui_page_next() == 0);
    CHECK(gy_param_ui_page() == 1);
    CHECK(gy_param_ui_page_prev() == 1);
    CHECK(gy_param_ui_page() == 0);
    CHECK(gy_param_ui_page_prev() == 0);
    return 0;
}

static int test_mode(void)
{
    gy_param_ui_init();
    CHECK(gy_param_ui_set_mode(GY_MODE_PVE) == 1);
    CHECK(gy_param_ui_mode() == GY_MODE_PVE);
    CHECK(gy_param_ui_page_next() == 1);
    CHECK(gy_param_ui_item_at(2) == GY_P_SEARCH_DEPTH);
    CHECK(gy_param_ui_set_mode(GY_MODE_PVP) == 1);
    CHECK(gy_param_ui_item_at(2) == -1);       /* 联机模式不显示搜索深度 */
    CHECK(gy_param_ui_set_mode(GY_MODE_COUNT) == 0);
    CHECK(gy_param_ui_set_mode(-1) == 0);
    CHECK(gy_param_ui_mode() == GY_MODE_PVP);
    return 0;
}

static int test_size_adjust(void)
{
    gy_param_ui_init();
    CHECK(gy_param_ui_value(GY_P_SIZE) == 6);
    CHECK(gy_param_ui_adjust(GY_P_SIZE, 1) == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 7);
    CHECK(gy_param_ui_adjust(GY_P_SIZE, 1) == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 8);
    CHECK(gy_param_ui_adjust(GY_P_SIZE, 1) == GY_PARAM_ERR_RANGE);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 8);
    CHECK(gy_param_ui_adjust(GY_P_SIZE, -4) == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 4);
    CHECK(gy_param_ui_adjust(GY_P_SIZE, -1) == GY_PARAM_ERR_RANGE);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 4);
    return 0;
}

static int test_spawn_adjust(void)
{
    gy_param_ui_init();
    CHECK(gy_param_ui_value(GY_P_SPAWN_INTERVAL) == 1);
    CHECK(gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 1) == GY_PARAM_ERR_ODD);
    CHECK(gy_param_ui_value(GY_P_SPAWN_INTERVAL) == 3);
    CHECK(gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 1) == GY_PARAM_ERR_ODD);
    CHECK(gy_param_ui_value(GY_P_SPAWN_INTERVAL) == 5);
    CHECK(gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 4) == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_SPAWN_INTERVAL) == 9);
    CHECK(gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 1) == GY_PARAM_ERR_RANGE);
    CHECK(gy_param_ui_value(GY_P_SPAWN_INTERVAL) == 9);
    return 0;
}

static int test_mode_independence(void)
{
    gy_param_ui_init();
    gy_param_reset(GY_MODE_SINGLE);
    gy_param_reset(GY_MODE_PVE);
    CHECK(gy_param_ui_adjust(GY_P_SIZE, -1) == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 5);
    CHECK(gy_param_ui_set_mode(GY_MODE_PVE) == 1);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 6);
    CHECK(gy_param_ui_adjust(GY_P_SIZE, -1) == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 5);
    CHECK(gy_param_ui_set_mode(GY_MODE_SINGLE) == 1);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 5);
    CHECK(gy_param_ui_set_mode(GY_MODE_PVE) == 1);
    CHECK(gy_param_ui_reset() == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 6);
    CHECK(gy_param_ui_set_mode(GY_MODE_SINGLE) == 1);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 5);
    return 0;
}

static int test_reset_and_apply(void)
{
    GyConfig cfg;
    GyGame game;
    gy_param_ui_init();
    CHECK(gy_param_ui_adjust(GY_P_HP, 12) == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_HP) == 20);
    CHECK(gy_param_ui_reset() == GY_PARAM_OK);
    CHECK(gy_param_ui_value(GY_P_HP) == 8);

    CHECK(gy_param_ui_adjust(GY_P_SIZE, -1) == GY_PARAM_OK);
    CHECK(gy_param_apply(GY_MODE_SINGLE, &cfg) == 1);
    CHECK(cfg.size == 5);
    CHECK(gy_init(&game, cfg, 20261015) == 1);
    CHECK(game.config.size == 5);
    return 0;
}

static int test_selftest(void)
{
    CHECK(gy_param_ui_selftest() == 0);
    /* 自检结束后必须回到默认值和初始分页，不能污染真实界面。 */
    CHECK(gy_param_ui_mode() == GY_MODE_SINGLE);
    CHECK(gy_param_ui_page() == 0);
    CHECK(gy_param_ui_value(GY_P_SIZE) == 6);
    CHECK(gy_param_ui_value(GY_P_SPAWN_INTERVAL) == 1);
    return 0;
}

int main(void)
{
    if (test_pages()) return 1;
    if (test_mode()) return 1;
    if (test_size_adjust()) return 1;
    if (test_spawn_adjust()) return 1;
    if (test_mode_independence()) return 1;
    if (test_reset_and_apply()) return 1;
    if (test_selftest()) return 1;
    printf("PASS: %d assertions; TC-407 parameter page state host tests\n", checks);
    return 0;
}
