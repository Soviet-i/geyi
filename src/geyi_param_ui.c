/*
 * geyi_param_ui.c —— 参数页纯逻辑状态实现（REQ-403/404/408）
 *
 * 分页规则：10 项参数按 GY_PARAM_UI_ROWS=5 分两页；当前模式下不适用的
 * 参数（目前只有人机专用的 search_depth）返回 -1，界面据此留空。
 * 增减直接调用 gy_param_set()，因此界面与主机测试共用同一套校验/钳位。
 */
#include "geyi_param_ui.h"

static int s_mode = GY_MODE_SINGLE;
static int s_page = 0;

/* 界面显示顺序：人机专用的搜索深度放第 2 页最后一行。
 * 单机/联机隐藏该行时，前面的参数仍连续排列，不会在中间留空。 */
static const int k_order[GY_P_COUNT] = {
    GY_P_SIZE, GY_P_SPAWN_INTERVAL, GY_P_HP, GY_P_ATTACK, GY_P_COST,
    GY_P_MERGE_REWARD, GY_P_KILL_REWARD, GY_P_DIR_BUFFER, GY_P_ANGLE_BUFFER,
    GY_P_SEARCH_DEPTH
};

static int mode_valid(int mode)
{
    return mode >= 0 && mode < GY_MODE_COUNT;
}

void gy_param_ui_init(void)
{
    s_mode = GY_MODE_SINGLE;
    s_page = 0;
    gy_param_init();
}

int gy_param_ui_mode(void)
{
    return s_mode;
}

int gy_param_ui_set_mode(int mode)
{
    if (!mode_valid(mode)) return 0;
    s_mode = mode;
    return 1;
}

int gy_param_ui_page(void)
{
    return s_page;
}

int gy_param_ui_page_count(void)
{
    return (GY_P_COUNT + GY_PARAM_UI_ROWS - 1) / GY_PARAM_UI_ROWS;
}

int gy_param_ui_page_next(void)
{
    int last = gy_param_ui_page_count() - 1;
    if (s_page >= last) return 0;
    ++s_page;
    return 1;
}

int gy_param_ui_page_prev(void)
{
    if (s_page <= 0) return 0;
    --s_page;
    return 1;
}

int gy_param_ui_item_at(int row)
{
    int id;
    if (row < 0 || row >= GY_PARAM_UI_ROWS) return -1;
    {int slot = s_page * GY_PARAM_UI_ROWS + row;
     if (slot < 0 || slot >= GY_P_COUNT) return -1;
     id = k_order[slot];}
    if (!gy_param_valid_in_mode(id, s_mode)) return -1;
    return id;
}

int gy_param_ui_value(int id)
{
    const GyParamSet *set = gy_param_mode(s_mode);
    if (!set) return 0;
    return gy_param_get(set, id);
}

int gy_param_ui_adjust(int id, int delta)
{
    GyParamSet *set = gy_param_mode(s_mode);
    int current;
    if (!set) return GY_PARAM_ERR_MODE;
    if (delta == 0) return GY_PARAM_OK;
    current = gy_param_get(set, id);
    {int raw=current+delta;
     /* 生成间隔必须为奇数：减 1 时不能先落到偶数再被 clamp 回原值，
      * 否则界面会出现“只能加不能减”。按调整方向跨到相邻奇数。 */
     if(id==GY_P_SPAWN_INTERVAL&&(raw&1)==0)raw+=delta>0?1:-1;
     return gy_param_set(set,id,raw);}
}

int gy_param_ui_reset(void)
{
    return gy_param_reset(s_mode);
}

int gy_param_ui_selftest(void)
{
    GyParamSet *p;
    GyConfig cfg;
    static GyGame game;   /* 自检不占主线程栈；板端首次上电曾出现返回码异常 */

    gy_param_ui_init();
    if (gy_param_ui_mode() != GY_MODE_SINGLE || gy_param_ui_page() != 0) return -1;
    if (gy_param_ui_page_count() != 2) return -2;
    if (gy_param_ui_item_at(0) != GY_P_SIZE) return -3;
    if (gy_param_ui_item_at(4) != GY_P_COST) return -4;
    if (gy_param_ui_item_at(-1) != -1 || gy_param_ui_item_at(GY_PARAM_UI_ROWS) != -1) return -5;

    if (gy_param_ui_page_next() != 1 || gy_param_ui_page() != 1) return -6;
    if (gy_param_ui_item_at(0) != GY_P_MERGE_REWARD) return -7;
    if (gy_param_ui_item_at(1) != GY_P_KILL_REWARD) return -8;
    if (gy_param_ui_item_at(2) != GY_P_DIR_BUFFER) return -9;
    if (gy_param_ui_item_at(3) != GY_P_ANGLE_BUFFER) return -10;
    if (gy_param_ui_item_at(4) != -1) return -11;  /* 单机模式不显示搜索深度（末行） */
    if (gy_param_ui_page_next() != 0 || gy_param_ui_page() != 1) return -12;
    if (gy_param_ui_page_prev() != 1 || gy_param_ui_page() != 0) return -13;
    if (gy_param_ui_page_prev() != 0 || gy_param_ui_page() != 0) return -14;

    if (!gy_param_ui_set_mode(GY_MODE_PVE)) return -15;
    gy_param_ui_page_next();
    if (gy_param_ui_item_at(4) != GY_P_SEARCH_DEPTH) return -16;
    if (gy_param_ui_set_mode(GY_MODE_COUNT)) return -17;  /* 非法模式不能被接受 */
    if (gy_param_ui_mode() != GY_MODE_PVE) return -18;

    /* 棋盘大小：6 -> 7 -> 8，再 + 越界应钳在 8；4 -> 3 应钳在 4。 */
    if (!gy_param_ui_set_mode(GY_MODE_SINGLE)) return -19;
    gy_param_reset(GY_MODE_SINGLE);
    if (gy_param_ui_value(GY_P_SIZE) != 6) return -20;
    if (gy_param_ui_adjust(GY_P_SIZE, 1) != GY_PARAM_OK || gy_param_ui_value(GY_P_SIZE) != 7) return -21;
    if (gy_param_ui_adjust(GY_P_SIZE, 1) != GY_PARAM_OK || gy_param_ui_value(GY_P_SIZE) != 8) return -22;
    if (gy_param_ui_adjust(GY_P_SIZE, 1) != GY_PARAM_ERR_RANGE || gy_param_ui_value(GY_P_SIZE) != 8) return -23;
    if (gy_param_ui_adjust(GY_P_SIZE, -4) != GY_PARAM_OK || gy_param_ui_value(GY_P_SIZE) != 4) return -24;
    if (gy_param_ui_adjust(GY_P_SIZE, -1) != GY_PARAM_ERR_RANGE || gy_param_ui_value(GY_P_SIZE) != 4) return -25;

    /* 生成间隔必须为奇数：1 -> 3 -> 5；9 再 + 应钳在 9。 */
    gy_param_reset(GY_MODE_SINGLE);
    if (gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 1) != GY_PARAM_OK ||
        gy_param_ui_value(GY_P_SPAWN_INTERVAL) != 3) return -26;
    if (gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 1) != GY_PARAM_OK ||
        gy_param_ui_value(GY_P_SPAWN_INTERVAL) != 5) return -27;
    if (gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 4) != GY_PARAM_OK ||
        gy_param_ui_value(GY_P_SPAWN_INTERVAL) != 9) return -28;
    if (gy_param_ui_adjust(GY_P_SPAWN_INTERVAL, 1) != GY_PARAM_ERR_RANGE ||
        gy_param_ui_value(GY_P_SPAWN_INTERVAL) != 9) return -29;

    /* 三类配置互相独立；重置只影响当前模式。 */
    gy_param_reset(GY_MODE_SINGLE);
    gy_param_reset(GY_MODE_PVE);
    gy_param_ui_set_mode(GY_MODE_SINGLE);
    gy_param_ui_adjust(GY_P_SIZE, -1);
    gy_param_ui_set_mode(GY_MODE_PVE);
    if (gy_param_ui_value(GY_P_SIZE) != 6) return -30;
    gy_param_ui_reset();
    if (gy_param_ui_value(GY_P_SIZE) != 6) return -31;
    gy_param_ui_set_mode(GY_MODE_SINGLE);
    if (gy_param_ui_value(GY_P_SIZE) != 5) return -32;

    /* 重置回默认值。 */
    gy_param_ui_adjust(GY_P_HP, 12);
    if (gy_param_ui_value(GY_P_HP) != 20) return -33;
    gy_param_ui_reset();
    if (gy_param_ui_value(GY_P_HP) != 8) return -34;

    /* 参数能转换成规则库接受的配置。 */
    p = gy_param_mode(GY_MODE_SINGLE);
    if (!p) return -35;
    gy_param_ui_adjust(GY_P_SIZE, -1);
    if (!gy_param_apply(GY_MODE_SINGLE, &cfg) || cfg.size != 5) return -36;
    if (!gy_init(&game, cfg, 20261015)) return -37;

    /* 自检不能污染真实界面：模式和分页也回到初始状态。 */
    gy_param_ui_init();
    return 0;
}
