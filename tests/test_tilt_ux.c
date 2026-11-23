/* 2026-11-09 收尾联调：倾斜输入的用户手势回归。
 *
 * 目标：把学生反馈的 5 个问题变成可复现的主机序列，且不依赖联机模块：
 *   1. 2 兵时 1↔2 档边界来回抖动，不允许出现 “0 个兵”/CANCEL；
 *   2. 数量档位默认每 10°，N 档放不下时退回按兵均分；
 *   3. 放兵列 = 离倾斜方向最近的己方底线空列；
 *   4. 轮到自己时板子已经斜着，也要在 250ms 内进入预览；
 *   5. 倾斜/屏幕仲裁在 geyi_ui.c，本文件只覆盖输入与映射（UI 由桌面用例覆盖）。
 */
#include "geyi_input.h"
#include "geyi_nav.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int checks;
#define CHECK(x) do {++checks;if(!(x)){printf("FAIL tilt-ux line %d: %s\n",__LINE__,#x);return 1;}}while(0)

typedef struct { GyInput input; uint32_t now; int available[4]; unsigned legal; } Simulation;

static Simulation setup(int avail)
{
    Simulation s; int d;
    memset(&s,0,sizeof(s));
    gy_input_reset(&s.input);
    for(d=0;d<4;++d)s.available[d]=avail;
    s.legal=0xF;
    return s;
}
static GyInputEvent sample(Simulation *s,int x,int y,int key)
{
    s->now+=5;
    return gy_input_update(&s->input,x,y,key,s->now,s->available,s->legal);
}
static int hold(Simulation *s,int x,int y,int key,int ticks)
{
    int events=0;
    while(ticks-->0)events|=sample(s,x,y,key).events;
    return events;
}
static int hold_count(int avail,int magnitude)
{
    Simulation s=setup(avail);
    hold(&s,0,0,0,8);
    hold(&s,0,-magnitude,0,60);
    return s.input.direction>=0?s.input.count:0;
}
static void empty_board(GyGame *g,int size)
{
    memset(g,0,sizeof(*g));
    g->config.size=size;
    g->outcome=GY_PLAYING;
}

/* 问题 1：1↔2 档边界手抖不得闪断预览。 */
static int issue1_count_rebound(void)
{
    Simulation s=setup(2);
    int i,events;
    hold(&s,0,0,0,8);
    hold(&s,0,-1000,0,45);
    CHECK(s.input.direction==GY_UP&&s.input.count==1);
    /* 在 17°（想切 2）/14°（想回 1）之间各停 100ms，短于 200ms 稳定窗口。 */
    for(i=0;i<12;++i){
        events=hold(&s,0,-1700,0,20);
        CHECK(!(events&GY_INPUT_CANCEL));
        CHECK(!(events&GY_INPUT_UPDATE));
        CHECK(s.input.direction==GY_UP&&s.input.count==1);
        events=hold(&s,0,-1400,0,20);
        CHECK(!(events&GY_INPUT_CANCEL));
        CHECK(!(events&GY_INPUT_UPDATE));
        CHECK(s.input.direction==GY_UP&&s.input.count==1);
    }
    /* 稳定越过 17° 才切到 2，且只发 UPDATE，不发 CANCEL。 */
    events=hold(&s,0,-1700,0,45);
    CHECK(events&GY_INPUT_UPDATE);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(s.input.direction==GY_UP&&s.input.count==2);
    /* 2→1 的下边界 15° 同理：1450/1700 各 100ms 不切换。 */
    for(i=0;i<12;++i){
        events=hold(&s,0,-1450,0,20);
        CHECK(!(events&GY_INPUT_CANCEL));
        CHECK(!(events&GY_INPUT_UPDATE));
        CHECK(s.input.count==2);
        events=hold(&s,0,-1700,0,20);
        CHECK(!(events&GY_INPUT_CANCEL));
        CHECK(!(events&GY_INPUT_UPDATE));
        CHECK(s.input.count==2);
    }
    events=hold(&s,0,-1400,0,45);
    CHECK(events&GY_INPUT_UPDATE);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(s.input.count==1);
    return 0;
}

/* 候选方向的可行动兵数与可见方向不同时，回到可见方向不得误 CANCEL。 */
static int pending_available_isolated(void)
{
    Simulation s=setup(2);
    int events;
    s.available[GY_RIGHT]=3;
    hold(&s,0,0,0,8);
    hold(&s,0,-2000,0,45);
    CHECK(s.input.direction==GY_UP&&s.input.count==2);
    /* 先向右倾斜到候选 RIGHT（可用 3），不足 200ms；再回到 UP。 */
    events=hold(&s,2400,-2100,0,20);
    CHECK(!(events&GY_INPUT_CANCEL));
    events=hold(&s,0,-2000,0,20);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(s.input.direction==GY_UP&&s.input.count==2);
    /* 稳定 205ms 后切 RIGHT：可用 3 与 UP 的 2 不同，也不得因 available 误 CANCEL。 */
    events=hold(&s,2400,-2100,0,45);
    CHECK(events&GY_INPUT_UPDATE);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(s.input.direction==GY_RIGHT&&s.input.count==2);
    return 0;
}

/* 明显指向一个没有可行动兵的方向：取消旧预览，不能确认旧方向。 */
static int unavailable_direction_cancels(void)
{
    Simulation s=setup(2);
    int events;
    s.available[GY_RIGHT]=0;
    s.legal&=~(1u<<GY_RIGHT);
    hold(&s,0,0,0,8);
    hold(&s,0,-2000,0,45);
    CHECK(s.input.direction==GY_UP&&s.input.count==2);
    events=hold(&s,2400,-2100,0,45);
    CHECK(events&GY_INPUT_CANCEL);
    CHECK(s.input.direction==-1);
    return 0;
}

/* 问题 2：默认每 10° 一档；放不下时按兵均分。 */
static int issue2_count_bands(void)
{
    CHECK(hold_count(2,600)==1);
    CHECK(hold_count(2,1500)==1);
    CHECK(hold_count(2,1700)==2);
    CHECK(hold_count(2,6000)==2);
    CHECK(hold_count(1,6000)==1);
    /* N=8：5400/8=675，2 档在 600+675+67=1342 之后。 */
    CHECK(hold_count(8,600)==1);
    CHECK(hold_count(8,1300)==1);
    CHECK(hold_count(8,1500)==2);
    CHECK(hold_count(8,6000)==8);
    /* N=6 仍放得下 10° 档：第 6 档 600+5*1000+100=5700。 */
    CHECK(hold_count(6,5600)==5);
    CHECK(hold_count(6,5800)==6);
    /* N=7 放不下：5400/7=771。 */
    CHECK(hold_count(7,1500)==2);
    CHECK(hold_count(7,6000)==7);
    return 0;
}

/* 6° 进入、4° 退出；回中才 CANCEL。 */
static int issue2_entry_exit_hysteresis(void)
{
    Simulation s=setup(2);
    int events;
    hold(&s,0,0,0,8);
    hold(&s,0,-1000,0,45);
    CHECK(s.input.direction==GY_UP);
    events=hold(&s,0,-500,0,45);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(s.input.direction==GY_UP);
    events=hold(&s,0,-450,0,45);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(s.input.direction==GY_UP);
    /* 4°~6° 回差带：轻微横向倾斜不得改方向；恰好 4.00° 仍保持。 */
    events=hold(&s,550,-500,0,45);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(!(events&GY_INPUT_UPDATE));
    CHECK(s.input.direction==GY_UP&&s.input.count==1);
    events=hold(&s,0,-400,0,45);
    CHECK(!(events&GY_INPUT_CANCEL));
    CHECK(s.input.direction==GY_UP);
    events=hold(&s,0,-399,0,10);
    CHECK(!(events&GY_INPUT_CANCEL));
    events=hold(&s,0,-399,0,16);
    CHECK(events&GY_INPUT_CANCEL);
    CHECK(s.input.direction==-1);
    events=hold(&s,0,-1100,0,45);
    CHECK(events&GY_INPUT_SELECT);
    CHECK(s.input.direction==GY_UP&&s.input.count==1);
    return 0;
}

/* 问题 4：换回合/重置后板子已经斜着，也要进入预览；确认后回中才能再选。 */
static int issue4_already_tilted_at_turn_start(void)
{
    Simulation s=setup(2);
    int i,select_tick=-1,events;
    hold(&s,0,0,0,8);
    gy_input_reset(&s.input); /* 模拟 revise 变化：板端 reset_input() */
    for(i=0;i<60;++i){
        if(sample(&s,0,-2000,0).events&GY_INPUT_SELECT){select_tick=i;break;}
    }
    CHECK(select_tick>=0&&select_tick<50); /* 250ms 内 */
    CHECK(s.input.direction==GY_UP&&s.input.count==2);
    /* 确认后保持倾斜：不得自动重新预览。 */
    events=hold(&s,0,-2000,1,6);
    CHECK(events&GY_INPUT_CONFIRM);
    CHECK(s.input.direction==-1);
    events=hold(&s,0,-2000,0,8);
    events|=hold(&s,0,-2000,1,40);
    CHECK(!(events&GY_INPUT_SELECT));
    CHECK(s.input.direction==-1);
    /* 回中低于 4° 后才允许重新选择。 */
    hold(&s,0,0,0,30);
    events=hold(&s,0,-2000,0,45);
    CHECK(events&GY_INPUT_SELECT);
    CHECK(s.input.direction==GY_UP&&s.input.count==2);
    return 0;
}

/* 板端 KEY2 确认后会 reset_input；若板子仍倾斜，换回合应重新预览（不要求先回中）。 */
static int board_reset_repreviews_when_tilted(void)
{
    Simulation s=setup(2);
    int i,select_tick=-1,events=0,selects=0,cancels=0;
    hold(&s,0,0,0,8);
    hold(&s,0,-2000,0,45);
    events=hold(&s,0,-2000,1,6);
    CHECK(events&GY_INPUT_CONFIRM);
    gy_input_reset(&s.input); /* KEY2 后 gy_board_poll 的 reset_input() */
    for(i=0;i<60;++i){
        GyInputEvent e=sample(&s,0,-2000,0);
        if(e.events&GY_INPUT_SELECT){selects++;if(select_tick<0)select_tick=i;}
        if(e.events&GY_INPUT_CANCEL)cancels++;
    }
    CHECK(select_tick>=0&&select_tick<50);
    CHECK(selects==1&&cancels==0); /* 第二局/换回合不得反复 SELECT/CANCEL */
    CHECK(s.input.direction==GY_UP&&s.input.count==2);
    return 0;
}

/* 方向竞争轴回差：10% 内保持原方向。 */
static int direction_buffer(void)
{
    Simulation s=setup(2);
    hold(&s,0,0,0,8);
    hold(&s,0,-2000,0,45);
    CHECK(s.input.direction==GY_UP);
    hold(&s,2000,-2100,0,45);
    CHECK(s.input.direction==GY_UP);
    hold(&s,2150,-2100,0,45);
    CHECK(s.input.direction==GY_UP);
    hold(&s,2400,-2100,0,60);
    CHECK(s.input.direction==GY_RIGHT);
    return 0;
}

/* 问题 3：放兵落点——最外围边/角上离方位角最近的空格；占用时沿射线内缩。 */
static int issue3_place_cell(void)
{
    GyGame g;GyInputPlace p;
    empty_board(&g,6);
    CHECK(!gy_input_place_cell(&g,0,0,-1,-1,&p));       /* 未倾斜不预选 */
    CHECK(gy_input_place_cell(&g,0,-2000,-1,-1,&p)&&p.row==0&&p.column==3);
    CHECK(gy_input_place_cell(&g,-1000,-2000,-1,-1,&p)&&p.row==0&&p.column==1);
    CHECK(gy_input_place_cell(&g,1000,-2000,-1,-1,&p)&&p.row==0&&p.column==4);
    CHECK(gy_input_place_cell(&g,-2000,0,-1,-1,&p)&&p.row==3&&p.column==0);
    CHECK(gy_input_place_cell(&g,2000,0,-1,-1,&p)&&p.row==3&&p.column==5);
    CHECK(gy_input_place_cell(&g,0,2000,-1,-1,&p)&&p.row==5&&p.column==3);
    CHECK(gy_input_place_cell(&g,2000,-1000,-1,-1,&p)&&p.row==1&&p.column==5);
    /* 右上角目标被占：沿同一列向下内缩到最近空格。 */
    g.cells[0][5].kind=GY_NUMBER;
    CHECK(gy_input_place_cell(&g,2000,-2000,-1,-1,&p)&&p.row==1&&p.column==5);
    g.cells[1][5].kind=GY_NUMBER;
    CHECK(gy_input_place_cell(&g,2000,-2000,-1,-1,&p)&&p.row==2&&p.column==5);
    /* 正上方目标被占：同列向下内缩。 */
    empty_board(&g,6);g.cells[0][3].kind=GY_NUMBER;
    CHECK(gy_input_place_cell(&g,0,-2000,-1,-1,&p)&&p.row==1&&p.column==3);
    /* 4° 死区保持当前目标。 */
    CHECK(gy_input_place_cell(&g,200,200,1,3,&p)&&p.row==1&&p.column==3);
    /* 射线全满则无目标，不退到别的边。 */
    empty_board(&g,6);{int y;for(y=0;y<6;++y)g.cells[y][3].kind=GY_NUMBER;}
    CHECK(!gy_input_place_cell(&g,0,-2000,-1,-1,&p));
    /* 4/5/7/8 与超大角度：映射不越界。 */
    empty_board(&g,4);
    CHECK(gy_input_place_cell(&g,0,-2000,-1,-1,&p)&&p.row==0&&p.column==2);
    CHECK(gy_input_place_cell(&g,2000,0,-1,-1,&p)&&p.row==2&&p.column==3);
    empty_board(&g,5);
    CHECK(gy_input_place_cell(&g,0,-2000,-1,-1,&p)&&p.row==0&&p.column==2);
    CHECK(gy_input_place_cell(&g,2000,0,-1,-1,&p)&&p.row==2&&p.column==4);
    empty_board(&g,7);
    CHECK(gy_input_place_cell(&g,0,-2000,-1,-1,&p)&&p.row==0&&p.column==3);
    CHECK(gy_input_place_cell(&g,2000,0,-1,-1,&p)&&p.row==3&&p.column==6);
    empty_board(&g,8);
    CHECK(gy_input_place_cell(&g,0,-2000,-1,-1,&p)&&p.row==0&&p.column==4);
    CHECK(gy_input_place_cell(&g,-9000,-9000,-1,-1,&p)&&p.row==0&&p.column==0);
    return 0;
}

/* P1 回归：x 有 0.2° 双轴噪声时，aim 不能把 SELECT 的 200ms 稳定窗口卡死。 */
static int dual_axis_noise_selects(void)
{
    Simulation s=setup(2);
    int i,selected=0,cancels=0,updates=0;
    for(i=0;i<600;++i){
        GyInputEvent e=sample(&s,(i&1)?20:0,-1600,0);
        if(e.events&GY_INPUT_SELECT)selected=1;
        if(e.events&GY_INPUT_CANCEL)cancels++;
        if(e.events&GY_INPUT_UPDATE)updates++;
    }
    CHECK(selected);
    CHECK(cancels==0);
    CHECK(updates==0); /* 1% 级 aim 噪声不得触发 UPDATE */
    CHECK(s.input.direction==GY_UP);
    return 0;
}
/* 第二局/切页：session 变化必须清旧选择，且新一局只 SELECT 一次。 */
static int session_reset_is_clean(void)
{
    Simulation s=setup(2);uint32_t last=1;int i,selects=0,cancels=0;
    hold(&s,0,0,0,8);hold(&s,0,-2000,0,45);
    CHECK(s.input.direction==GY_UP);
    CHECK(gy_input_begin_session(&s.input,2,&last)==1);
    CHECK(last==2&&s.input.direction==-1);
    CHECK(gy_input_begin_session(&s.input,2,&last)==0);
    for(i=0;i<60;++i){
        GyInputEvent e=sample(&s,0,-2000,0);
        if(e.events&GY_INPUT_SELECT)++selects;
        if(e.events&GY_INPUT_CANCEL)++cancels;
    }
    CHECK(selects==1&&cancels==0);
    CHECK(s.input.direction==GY_UP);
    return 0;
}
/* 缓慢连续转动：不需要回正，持续偏离 200ms 后应切到最新方位。 */
static int aim_continuous_rotation(void)
{
    Simulation s=setup(2);int i,updates=0;
    hold(&s,0,0,0,8);
    hold(&s,-300,-600,0,45);         /* 左上，aim=-50 */
    CHECK(s.input.direction==GY_UP&&s.input.aim==-50);
    for(i=0;i<120;++i){
        int x=-300+600*i/120;
        GyInputEvent e=sample(&s,x,-600,0);
        if(e.events&GY_INPUT_UPDATE)++updates;
    }
    CHECK(updates>0);
    CHECK(s.input.direction==GY_UP);
    CHECK(s.input.aim>0);            /* 已切到右倾，不需要先回正 */
    return 0;
}
/* P2 复核：aim 接近 10% 边界时不得每帧 UPDATE；持续偏移才更新一次。 */
static int aim_boundary_hysteresis(void)
{
    Simulation s=setup(2);int i,updates=0;
    hold(&s,0,0,0,8);
    hold(&s,300,-600,0,45);          /* 6°，aim=50 */
    CHECK(s.input.direction==GY_UP);
    for(i=0;i<600;++i){
        GyInputEvent e=sample(&s,(i&1)?180:300,-600,0);
        if(e.events&GY_INPUT_UPDATE)++updates;
    }
    CHECK(updates==0);
    for(i=0;i<45;++i){
        GyInputEvent e=sample(&s,180,-600,0); /* aim=30，持续 225ms */
        if(e.events&GY_INPUT_UPDATE)++updates;
    }
    CHECK(updates==1);
    CHECK(s.input.aim==30);
    return 0;
}
/* 固定种子的手抖：已出现的预览不得因噪声消失（CANCEL=0）。 */
static uint32_t rng_state=20261109u;
static int noise(int amplitude)
{
    rng_state=rng_state*1103515245u+12345u;
    return (int)((rng_state>>16)%(uint32_t)(2*amplitude+1))-amplitude;
}
static int tremor_keeps_preview(void)
{
    Simulation s=setup(2);
    int i,selected=0,cancels=0,zero=0;
    /* 站在 16° 档位边界附近，模拟 20ms 采样的手持抖动。 */
    for(i=0;i<600;++i){
        int y=-1600+noise(300);
        GyInputEvent e;
        s.now+=20;
        e=gy_input_update(&s.input,0,y,0,s.now,s.available,s.legal);
        if(e.events&GY_INPUT_SELECT)selected=1;
        if(selected&&(e.events&GY_INPUT_CANCEL))++cancels;
        if(selected&&s.input.direction<0)++zero;
    }
    CHECK(selected);
    CHECK(cancels==0);
    CHECK(zero==0);
    CHECK(s.input.direction==GY_UP);
    CHECK(s.input.count>=1&&s.input.count<=2);
    return 0;
}

/* 问题 5：倾斜/按钮来源仲裁（UI 接线由桌面用例覆盖，这里测状态机）。 */
static int issue5_nav_arbitration(void)
{
    GyNav nav;
    gy_nav_reset(&nav);
    CHECK(gy_nav_tilt_allowed(&nav));
    CHECK(gy_nav_tilt_selected(&nav));
    CHECK(gy_nav_tilt_active(&nav)&&!gy_nav_button_active(&nav));
    CHECK(gy_nav_blocks_screen_edit(&nav,0));
    CHECK(!gy_nav_blocks_screen_edit(&nav,1));
    CHECK(gy_nav_tilt_cancel(&nav));
    CHECK(!gy_nav_tilt_active(&nav)&&gy_nav_tilt_allowed(&nav));
    CHECK(!gy_nav_tilt_cancel(&nav));
    /* 按钮预览：倾斜被拒绝，回中也不打断。 */
    CHECK(gy_nav_button_selected(&nav));
    CHECK(!gy_nav_tilt_allowed(&nav));
    CHECK(!gy_nav_tilt_selected(&nav));
    CHECK(gy_nav_button_active(&nav)&&!gy_nav_tilt_active(&nav));
    CHECK(!gy_nav_tilt_cancel(&nav));
    CHECK(gy_nav_button_active(&nav));
    CHECK(gy_nav_clear(&nav));
    CHECK(gy_nav_tilt_allowed(&nav));
    /* 倾斜预览中屏幕编辑动作被挡住。 */
    gy_nav_tilt_selected(&nav);
    CHECK(gy_nav_blocks_screen_edit(&nav,0)==1);
    gy_nav_clear(&nav);
    gy_nav_button_selected(&nav);
    CHECK(gy_nav_tilt_selected(&nav)==0);
    return 0;
}

int main(void)
{
    CHECK(gy_input_selftest());
    CHECK(issue1_count_rebound()==0);
    CHECK(pending_available_isolated()==0);
    CHECK(unavailable_direction_cancels()==0);
    CHECK(issue2_count_bands()==0);
    CHECK(issue2_entry_exit_hysteresis()==0);
    CHECK(issue4_already_tilted_at_turn_start()==0);
    CHECK(board_reset_repreviews_when_tilted()==0);
    CHECK(direction_buffer()==0);
    CHECK(issue3_place_cell()==0);
    CHECK(tremor_keeps_preview()==0);
    CHECK(dual_axis_noise_selects()==0);
    CHECK(session_reset_is_clean()==0);
    CHECK(aim_boundary_hysteresis()==0);
    CHECK(aim_continuous_rotation()==0);
    CHECK(issue5_nav_arbitration()==0);
    printf("PASS: tilt-ux %d assertions; simulated 20ms/5ms samples, no board\n",checks);
    return 0;
}
