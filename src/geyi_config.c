/*
 * geyi_config.c —— 游戏参数配置子系统实现（REQ-401~411）
 *
 * 设计依据：docs/02-方案设计说明.md、docs/adr/0003-param-model.md
 *
 * 校验策略分两层：
 *   gy_param_validate() 返回具体错误码，供界面提示与主机测试精确断言；
 *   gy_param_clamp()    在设置入口做边界钳位，保证进入模型的数值一定合法。
 * 两者共用同一张只读范围表，避免"界面一套、校验一套"。
 *
 * 纯 C，不依赖 LVGL / RT-Thread / 板级头文件，可在主机端直接编译测试。
 */
#include "geyi_config.h"

/* ---- 元信息表：名称、范围、适用模式。界面与校验共用。 ---- */
typedef struct {
    const char *name;
    int         min;
    int         max;
    int         pve_only;
    int         must_be_odd;
} GyParamMeta;

static const GyParamMeta k_meta[GY_P_COUNT] = {
    { "棋盘大小",      GY_SIZE_MIN,    GY_SIZE_MAX,    0, 0 },
    { "生成间隔",      GY_SPAWN_MIN,   GY_SPAWN_MAX,   0, 1 },
    { "士兵血量",      GY_HP_MIN,      GY_HP_MAX,      0, 0 },
    { "士兵攻击力",    GY_ATTACK_MIN,  GY_ATTACK_MAX,  0, 0 },
    { "放置费",        GY_COST_MIN,    GY_COST_MAX,    0, 0 },
    { "合并奖励",      GY_MERGE_MIN,   GY_MERGE_MAX,   0, 0 },
    { "击杀奖励",      GY_KILL_MIN,    GY_KILL_MAX,    0, 0 },
    { "AI搜索深度",    GY_DEPTH_MIN,   GY_DEPTH_MAX,   1, 0 },  /* 仅人机模式 */
    { "方向缓冲阈值",  GY_BUFFER_MIN,  GY_BUFFER_MAX,  0, 0 },
    { "角度缓冲阈值",  GY_BUFFER_MIN,  GY_BUFFER_MAX,  0, 0 },
};

/* 三类配置存在 RAM 中，重启回默认（ADR-0003）。 */
static GyParamSet s_params[GY_MODE_COUNT];
static int        s_inited = 0;

/* 编译期断言：范围表条目数必须与 GY_P_COUNT 一致。
 * 若有人新增参数却忘了加表项，这里会直接编译失败，而不是运行时才发现。 */
typedef char gy_param_count_check[(GY_P_COUNT == 10) ? 1 : -1];

static int mode_valid(int mode)
{
    return mode >= 0 && mode < GY_MODE_COUNT;
}

static int id_valid(int id)
{
    return id >= 0 && id < GY_P_COUNT;
}

const char *gy_param_name(int id)
{
    return id_valid(id) ? k_meta[id].name : "";
}

int gy_param_range(int id, int *min_out, int *max_out)
{
    if (!id_valid(id)) return GY_PARAM_ERR_ID;
    if (min_out) *min_out = k_meta[id].min;
    if (max_out) *max_out = k_meta[id].max;
    return GY_PARAM_OK;
}

int gy_param_valid_in_mode(int id, int mode)
{
    if (!id_valid(id) || !mode_valid(mode)) return 0;
    if (k_meta[id].pve_only && mode != GY_MODE_PVE) return 0;
    return 1;
}

GyParamSet gy_param_default_for(int mode)
{
    /* 默认值 = 改造前的硬编码值，保证参数化前后行为一致（便于用 TC-301~308 回归）。
     * 原 gy_default_config() 为 {6, 8, 2, 4, 1, 2}，spawn_interval 改造前等价于每次=1，
     * 两处倾斜滞回改造前硬编码为 10%。 */
    GyParamSet s;
    (void)mode;                 /* 目前三类默认值相同；差异由用户配置产生 */
    s.size           = 6;
    s.spawn_interval = 1;
    s.hp             = 8;
    s.attack         = 2;
    s.cost           = 4;
    s.merge_reward   = 1;
    s.kill_reward    = 2;
    s.search_depth   = 3;
    s.dir_buffer     = 10;
    s.angle_buffer   = 10;
    return s;
}

int gy_param_validate(int id, int value)
{
    const GyParamMeta *m;
    if (!id_valid(id)) return GY_PARAM_ERR_ID;
    m = &k_meta[id];
    if (value < m->min || value > m->max) return GY_PARAM_ERR_RANGE;
    if (m->must_be_odd && (value % 2) == 0) return GY_PARAM_ERR_ODD;
    return GY_PARAM_OK;
}

int gy_param_clamp(int id, int value)
{
    const GyParamMeta *m;
    if (!id_valid(id)) return 0;
    m = &k_meta[id];
    if (value < m->min) value = m->min;
    if (value > m->max) value = m->max;
    if (m->must_be_odd && (value % 2) == 0)
    {
        /* 钳到区间内最近的奇数，优先向上；向上越界则向下 */
        if (value + 1 <= m->max) value += 1;
        else                    value -= 1;
    }
    return value;
}

int gy_param_get(const GyParamSet *set, int id)
{
    if (!set || !id_valid(id)) return 0;
    switch (id)
    {
    case GY_P_SIZE:           return set->size;
    case GY_P_SPAWN_INTERVAL: return set->spawn_interval;
    case GY_P_HP:             return set->hp;
    case GY_P_ATTACK:         return set->attack;
    case GY_P_COST:           return set->cost;
    case GY_P_MERGE_REWARD:   return set->merge_reward;
    case GY_P_KILL_REWARD:    return set->kill_reward;
    case GY_P_SEARCH_DEPTH:   return set->search_depth;
    case GY_P_DIR_BUFFER:     return set->dir_buffer;
    case GY_P_ANGLE_BUFFER:   return set->angle_buffer;
    default:                  return 0;
    }
}

static void put_field(GyParamSet *set, int id, int value)
{
    switch (id)
    {
    case GY_P_SIZE:           set->size           = value; break;
    case GY_P_SPAWN_INTERVAL: set->spawn_interval = value; break;
    case GY_P_HP:             set->hp             = value; break;
    case GY_P_ATTACK:         set->attack         = value; break;
    case GY_P_COST:           set->cost           = value; break;
    case GY_P_MERGE_REWARD:   set->merge_reward   = value; break;
    case GY_P_KILL_REWARD:    set->kill_reward    = value; break;
    case GY_P_SEARCH_DEPTH:   set->search_depth   = value; break;
    case GY_P_DIR_BUFFER:     set->dir_buffer     = value; break;
    case GY_P_ANGLE_BUFFER:   set->angle_buffer   = value; break;
    default: break;
    }
}

int gy_param_set(GyParamSet *set, int id, int value)
{
    int rc;
    if (!set) return GY_PARAM_ERR_ID;
    rc = gy_param_validate(id, value);
    /* 越界一律钳位写入（界面据此提示），但返回码如实反映原始输入是否合法 */
    put_field(set, id, gy_param_clamp(id, value));
    return rc;
}

int gy_param_init(void)
{
    int mode;
    for (mode = 0; mode < GY_MODE_COUNT; ++mode)
        s_params[mode] = gy_param_default_for(mode);
    s_inited = 1;
    return GY_PARAM_OK;
}

GyParamSet *gy_param_mode(int mode)
{
    if (!mode_valid(mode)) return 0;
    if (!s_inited) gy_param_init();
    return &s_params[mode];
}

int gy_param_reset(int mode)
{
    if (!mode_valid(mode)) return GY_PARAM_ERR_ID;
    if (!s_inited) gy_param_init();
    s_params[mode] = gy_param_default_for(mode);
    return GY_PARAM_OK;
}

void gy_param_to_game_config(const GyParamSet *set, GyConfig *out)
{
    if (!set || !out) return;
    out->size           = set->size;
    out->hp             = set->hp;
    out->attack         = set->attack;
    out->cost           = set->cost;
    out->merge_reward   = set->merge_reward;
    out->kill_reward    = set->kill_reward;
    out->spawn_interval = set->spawn_interval;
    /* search_depth 与两项 buffer 不属于 GyConfig：前者由 AI 模块读取，
     * 后者由 gy_input_configure() 读取，见 docs/02 的接线说明。 */
}

int gy_param_apply(int mode, GyConfig *out)
{
    if (!mode_valid(mode) || !out) return 0;
    if (!s_inited) gy_param_init();
    gy_param_to_game_config(&s_params[mode], out);
    return 1;
}

int gy_param_selftest(void)
{
    int i, mode;

    /* 1) 范围表条目数必须与参数枚举一致 —— 已在编译期由 gy_param_count_check 保证，
     *    此处不再做运行期常量比较（MSVC 的 C4127 会把常量条件当警告/错误）。 */
    (void)0;

    /* 2) 每项范围合法，且默认值落在范围内并通过校验 */
    for (mode = 0; mode < GY_MODE_COUNT; ++mode)
    {
        GyParamSet s = gy_param_default_for(mode);
        for (i = 0; i < GY_P_COUNT; ++i)
        {
            int lo = 0, hi = 0;
            if (gy_param_range(i, &lo, &hi) != GY_PARAM_OK) return -2;
            if (lo > hi) return -3;
            if (gy_param_validate(i, gy_param_get(&s, i)) != GY_PARAM_OK) return -4;
        }
    }

    /* 3) 钳位边界：低于下限取下限，高于上限取上限 */
    for (i = 0; i < GY_P_COUNT; ++i)
    {
        int lo = 0, hi = 0;
        gy_param_range(i, &lo, &hi);
        if (gy_param_clamp(i, lo - 1000) != lo) return -5;
        if (gy_param_clamp(i, hi + 1000) != hi) return -6;
    }

    /* 4) 生成间隔必须为奇数：偶数应被校验拒绝，且钳位结果是奇数 */
    if (gy_param_validate(GY_P_SPAWN_INTERVAL, 4) != GY_PARAM_ERR_ODD) return -7;
    if ((gy_param_clamp(GY_P_SPAWN_INTERVAL, 4) % 2) == 0) return -8;
    if (gy_param_validate(GY_P_SPAWN_INTERVAL, 3) != GY_PARAM_OK) return -9;

    /* 5) search_depth 仅人机模式适用 */
    if (!gy_param_valid_in_mode(GY_P_SEARCH_DEPTH, GY_MODE_PVE))  return -10;
    if (gy_param_valid_in_mode(GY_P_SEARCH_DEPTH, GY_MODE_SINGLE)) return -11;
    if (gy_param_valid_in_mode(GY_P_SEARCH_DEPTH, GY_MODE_PVP))    return -12;

    /* 6) 非法标识必须报错，不能静默通过 */
    if (gy_param_validate(-1, 1) != GY_PARAM_ERR_ID)   return -13;
    if (gy_param_validate(GY_P_COUNT, 1) != GY_PARAM_ERR_ID) return -14;

    /* 7) 三类配置互相独立：改一类不影响另一类 */
    gy_param_init();
    gy_param_mode(GY_MODE_SINGLE)->size = 5;
    if (gy_param_mode(GY_MODE_PVE)->size != 6) return -15;
    if (gy_param_mode(GY_MODE_PVP)->size != 6) return -16;

    /* 8) 重置能回到默认 */
    gy_param_reset(GY_MODE_SINGLE);
    if (gy_param_mode(GY_MODE_SINGLE)->size != 6) return -17;

    return 0;
}
