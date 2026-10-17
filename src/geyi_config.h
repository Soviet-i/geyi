/*
 * geyi_config.h —— 游戏参数配置子系统（REQ-401~411）
 *
 * 设计依据：docs/02-方案设计说明.md「参数配置与平衡性测试设计」、docs/adr/0003-param-model.md
 *
 * 覆盖《想法》稿要求的 10 项参数；单机/人机/联机三类配置互相独立、各自可重置。
 * 本模块只维护参数与校验，不改动移动/合并/攻击/金币/胜负的结算逻辑。
 */
#ifndef GEYI_CONFIG_H
#define GEYI_CONFIG_H

#include "geyi_game.h"

/* 三类游戏参数互相独立：《想法》稿要求单机/人机/联机分别自定义。 */
enum { GY_MODE_SINGLE = 0, GY_MODE_PVE, GY_MODE_PVP, GY_MODE_COUNT };

/* 10 项参数；枚举顺序即界面显示顺序。 */
enum {
    GY_P_SIZE = 0,        /* 棋盘大小 N×N */
    GY_P_SPAWN_INTERVAL,  /* 多少次有效移动生成一个数字单位（必须为奇数） */
    GY_P_HP,              /* 士兵血量 */
    GY_P_ATTACK,          /* 士兵攻击力 */
    GY_P_COST,            /* 放置一个士兵所需金币 */
    GY_P_MERGE_REWARD,    /* 完成一次合并获得金币 */
    GY_P_KILL_REWARD,     /* 击杀一个单位获得金币 */
    GY_P_SEARCH_DEPTH,    /* AI 搜索深度（仅人机模式） */
    GY_P_DIR_BUFFER,      /* 倾斜方向切换缓冲阈值（百分比） */
    GY_P_ANGLE_BUFFER,    /* 倾斜角度切换缓冲阈值（百分比） */
    GY_P_COUNT
};

/* 取值范围：集中定义，避免魔法数散落。
 * 与 geyi_game.c 中 gy_init() 的校验保持"一致或更严"，保证本模块产出的配置一定能被接受。 */
#define GY_SIZE_MIN            4      /* 与 gy_init 一致（gy_init 要求 4..GY_MAX） */
#define GY_SIZE_MAX            8      /* 与 gy_init 一致（GY_MAX） */
#define GY_SPAWN_MIN           1
#define GY_SPAWN_MAX           9
#define GY_HP_MIN              1
#define GY_HP_MAX              64
#define GY_ATTACK_MIN          1
#define GY_ATTACK_MAX          16
#define GY_COST_MIN            1      /* gy_init 要求 cost>=1，不能放宽到 0 */
#define GY_COST_MAX            20
#define GY_MERGE_MIN           0
#define GY_MERGE_MAX           5
#define GY_KILL_MIN            0
#define GY_KILL_MAX            10
#define GY_DEPTH_MIN           1
#define GY_DEPTH_MAX           6
#define GY_BUFFER_MIN          0
#define GY_BUFFER_MAX          50

/* 10 项参数的显式字段（ADR-0003：用结构体字段而非通用键值数组）。 */
typedef struct {
    int size;
    int spawn_interval;
    int hp;
    int attack;
    int cost;
    int merge_reward;
    int kill_reward;
    int search_depth;
    int dir_buffer;
    int angle_buffer;
} GyParamSet;

/* 返回码：validate 必须给出具体原因，而不是笼统失败（REQ-402 的测试要按码断言）。 */
#define GY_PARAM_OK            0
#define GY_PARAM_ERR_ID      (-1)   /* 参数标识非法 */
#define GY_PARAM_ERR_RANGE   (-2)   /* 超出取值范围 */
#define GY_PARAM_ERR_ODD     (-3)   /* 必须为奇数 */
#define GY_PARAM_ERR_MODE    (-4)   /* 该参数在此模式下不适用 */

/* ---- 元信息（界面渲染与校验共用同一张表）---- */
const char *gy_param_name(int id);
int         gy_param_range(int id, int *min_out, int *max_out);
int         gy_param_valid_in_mode(int id, int mode);   /* 1=适用，0=不适用 */

/* ---- 纯函数：默认值、校验、钳位、读写 ---- */
GyParamSet  gy_param_default_for(int mode);
int         gy_param_validate(int id, int value);       /* 返回 GY_PARAM_* 码 */
int         gy_param_clamp(int id, int value);          /* 返回钳位后的合法值 */
int         gy_param_get(const GyParamSet *set, int id);
int         gy_param_set(GyParamSet *set, int id, int value);  /* 校验+钳位+写入，返回码 */

/* ---- 三类配置存储（RAM，重启回默认；见 ADR-0003）---- */
int          gy_param_init(void);                       /* 三类都初始化为默认值 */
GyParamSet  *gy_param_mode(int mode);                   /* 可写指针；非法模式返回 NULL */
int          gy_param_reset(int mode);                  /* 重置该模式为官方默认 */

/* ---- 到规则库的转换（只在"新开一局"时调用一次，保证局中改参数不影响当前局）---- */
void         gy_param_to_game_config(const GyParamSet *set, GyConfig *out);
int          gy_param_apply(int mode, GyConfig *out);   /* 内部按 mode 取参数并转换 */

/* ---- 主机/板端自检：返回 0 表示通过 ---- */
int          gy_param_selftest(void);

#endif /* GEYI_CONFIG_H */
