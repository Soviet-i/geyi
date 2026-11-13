#ifdef GEYI_WITH_LVGL
#include "geyi_ui.h"
#include "geyi_ai.h"
#include "geyi_ai_task.h"
#include "geyi_config.h"
#include "geyi_param_ui.h"
#include "geyi_nav.h"
#include "geyi_history.h"
#include "geyi_timer.h"
#include "geyi_net_app.h"
#include "geyi_font_cn16.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GEYI_WITH_RTTHREAD
#include "finsh.h"
#include "geyi_board.h"
#include "geyi_history_store.h"
#endif

static GyGame game;
static GyPreview preview;
static int direction=-1, amount=1, selected_count=1, aim=0;
static int placing=0, place_row=-1, place_column=-1, ready=0;
/* 输入源仲裁（REQ-319）：倾斜预览/按钮预览互斥，逻辑在 geyi_nav.c 主机可测。 */
static GyNav nav;
/* 页面/新局代次：板端发现变化即重置倾斜会话，避免第二局沿用上一局状态。 */
static uint32_t ui_session;
/* 历史记录与单局计时（REQ-601~REQ-606）。 */
static lv_obj_t *history_page,*history_detail_page;
static lv_obj_t *hist_rows[5],*hist_row_cells[5][4],*hist_header[4],*hist_empty_label;
static lv_obj_t *hist_clear_button,*hist_clear_label;
static lv_obj_t *hist_detail_lines[7];
static int hist_index,hist_confirm_clear;
static GyTimer match_timer;
static GyTimer net_match_timer;
static int match_recorded,match_resigner,net_match_recorded;
static void history_refresh(void);
static void history_reset_confirm(void);
static void history_detail_open(int index);
static void history_record_if_finished(void);
static void history_net_tick(uint32_t now);
static lv_obj_t *ui_page;
static lv_obj_t *tiles[GY_MAX][GY_MAX], *labels[GY_MAX][GY_MAX], *health[GY_MAX][GY_MAX];
static lv_obj_t *turn_label,*money_label,*hint,*count_label,*confirm_button;
static lv_obj_t *home_page,*mode_page,*game_page,*prep_page,*param_page,*rules_page;
static lv_obj_t *rules_content,*rules_page_label;
static int rules_index,rules_active;
static lv_obj_t *net_lobby_page,*net_room_page;
static lv_obj_t *net_lobby_status,*net_room_status;
static lv_obj_t *net_room_rows[5];
static lv_obj_t *net_red_name,*net_red_state,*net_blue_name,*net_blue_state;
static lv_obj_t *net_spectator_label;
static lv_obj_t *net_red_box,*net_blue_box,*net_room_hint;
static lv_obj_t *net_ready_button,*net_leave_button,*place_button,*resign_button;
static lv_obj_t *net_result_overlay,*net_result_title,*net_result_sub;
static lv_obj_t *net_reconn_overlay,*net_reconn_label;
static int net_last_reconn=-1;
static lv_obj_t *net_confirm_block,*net_confirm_overlay,*net_confirm_label;
static void net_confirm_hide(void);
static int net_lobby_active,net_room_active,net_game_active;
static uint32_t net_last_rev;static int net_last_phase=-1;
static uint32_t net_terminal_at;   /* 对方断线 10s 后的本地提示停留起点 */
static lv_obj_t *prep_red_label,*prep_blue_label;
static lv_obj_t *prep_player_icon[2],*prep_ai_icon[2];
static lv_obj_t *param_mode_buttons[GY_MODE_COUNT];
static lv_obj_t *param_row_obj[GY_PARAM_UI_ROWS];
static lv_obj_t *param_row_name[GY_PARAM_UI_ROWS];
static lv_obj_t *param_row_value[GY_PARAM_UI_ROWS];
static lv_obj_t *param_page_label,*param_status_label;
static lv_obj_t *param_prev_button,*param_next_button;
static int prep_active;
static int prep_human_red=1;
static int playing;
static int param_active;
static int feedback;
static int game_mode=GY_MODE_SINGLE;
static int ai_side=-1;          /* -1 = no AI, 0 = AI red, 1 = AI blue */
static int ai_thinking;
static int ai_preview_pending;
static uint32_t ai_preview_at;
static GyAiAction ai_pending;
static uint32_t ai_generation;
static uint32_t ai_revision;
static char param_status_text[32]="新开一局生效";
#define AI_PREVIEW_MS 500u
/* 游戏规则页：正文逐字采用学生给出的表述；分页只加页码和续页标题。 */
#define RULES_PAGES 4
static const char *const rules_text[RULES_PAGES]={
"朝某方向移动：我方士兵朝该方向走一格，数字单位像 2048 一样滑到最远并合并。\n\n"
"金币与放兵：合并数字或击杀士兵获得金币。花金币可放士兵，放置士兵和移动一样，算一个回合，执行后轮到对方。\n\n"
"伤害：士兵移动被敌人挡住就攻击；数字单位撞到任何士兵都会造成等于该数字的伤害。士兵有血量，而数字单位是无敌的。\n\n"
"胜负：双方轮流行动，先消灭对方全部士兵者获胜；若双方士兵同时全灭则平局。",
"操作方式\n\n"
"1. 选方向：把板子朝想走的方向倾斜\n\n"
"2. 选人数：倾斜角度决定本次移动几个我方士兵，角度越大移动的士兵越多\n\n"
"3. 确认：按板子侧方的KEY2确认落子\n\n"
"4. 放兵：板子未倾斜时，按KEY2可进入/退出放兵模式（选方向并确认即可放兵）\n\n"
"（同时兼容触屏的操作方式，屏幕的“确认”等同于KEY2）",
"操作判定逻辑\n\n"
"1. 谁先移动：优先移动离倾斜方向最近的可移动士兵。\n\n"
"2. 选人数：若可移动士兵有 N 个，每 10° 一档，每多倾斜一档，多移动 1 个士兵；\n\n"
"若 N 档大于 60° 上限，才把“下限到上限”平均分成 N 档。\n\n"
"（例：可移动士兵 2 个，6° 进入预览，则 6°~16° 移动 1 个，16°~26° 移动 2 个，最多 2 个。并且设有角度缓冲。）",
"操作判定逻辑（续）\n\n"
"3. 放兵：在己方底线中，士兵只能放置在最靠近倾斜方向且未被占用的格子里。\n\n"
"4. 预览：倾斜时进入预览模式，板子回正则取消预览"
};
static void cancel_ai(void)
{
    if(ai_thinking)gy_ai_task_cancel();
    ai_thinking=0;
    ai_preview_pending=0;
    ai_pending.kind=0;
    ++ai_generation;
}
static int human_turn(void)
{
    if(gy_net_app_active())return gy_net_app_local_turn();
    return ai_side<0||game.turn!=ai_side;
}
/* 放兵与方向预览在 UI 里是两套互斥状态；集中清理，避免某条路径漏字段。 */
static void clear_ui_placement(void)
{
    placing=0;place_row=-1;place_column=-1;
}
static void reset_selection_ui(void)
{
    ready=0;direction=-1;aim=0;clear_ui_placement();
}
static void show_page(lv_obj_t *page)
{
    cancel_ai();
    net_terminal_at=0;
    lv_obj_add_flag(home_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mode_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(game_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(prep_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(param_page,LV_OBJ_FLAG_HIDDEN);
    if(rules_page)lv_obj_add_flag(rules_page,LV_OBJ_FLAG_HIDDEN);
    if(net_lobby_page)lv_obj_add_flag(net_lobby_page,LV_OBJ_FLAG_HIDDEN);
    if(net_room_page)lv_obj_add_flag(net_room_page,LV_OBJ_FLAG_HIDDEN);
    if(history_page)lv_obj_add_flag(history_page,LV_OBJ_FLAG_HIDDEN);
    if(history_detail_page)lv_obj_add_flag(history_detail_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page,LV_OBJ_FLAG_HIDDEN);
    playing=page==game_page;
    prep_active=page==prep_page;
    param_active=page==param_page;
    rules_active=page==rules_page;
    net_lobby_active=page==net_lobby_page;
    net_room_active=page==net_room_page;
    net_game_active=page==game_page&&gy_net_app_active();
    net_last_reconn=-1;
    if(page!=game_page){
        if(net_result_overlay)lv_obj_add_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);
        if(net_reconn_overlay)lv_obj_add_flag(net_reconn_overlay,LV_OBJ_FLAG_HIDDEN);
        if(net_confirm_block)lv_obj_add_flag(net_confirm_block,LV_OBJ_FLAG_HIDDEN);
        if(net_confirm_overlay)lv_obj_add_flag(net_confirm_overlay,LV_OBJ_FLAG_HIDDEN);
    }
    if(page!=ui_page){ui_page=page;++ui_session;}
    gy_nav_clear(&nav);
    reset_selection_ui();
}
static void rules_refresh(void)
{
    char text[16];
    if(!rules_content||!rules_page_label)return;
    if(rules_index<0)rules_index=0;
    if(rules_index>=RULES_PAGES)rules_index=RULES_PAGES-1;
    lv_label_set_text(rules_content,rules_text[rules_index]);
    lv_snprintf(text,sizeof(text),"%d/%d",rules_index+1,RULES_PAGES);
    lv_label_set_text(rules_page_label,text);
}
static void update_prep_labels(void)
{
    int i;
    if(!prep_red_label||!prep_blue_label)return;
    for(i=0;i<2;++i){
        int human=(i==0)?prep_human_red:!prep_human_red;
        lv_label_set_text(i==0?prep_red_label:prep_blue_label,human?"玩家":"AI");
        if(prep_player_icon[i]){
            if(human)lv_obj_remove_flag(prep_player_icon[i],LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(prep_player_icon[i],LV_OBJ_FLAG_HIDDEN);
        }
        if(prep_ai_icon[i]){
            if(human)lv_obj_add_flag(prep_ai_icon[i],LV_OBJ_FLAG_HIDDEN);
            else lv_obj_remove_flag(prep_ai_icon[i],LV_OBJ_FLAG_HIDDEN);
        }
    }
}
static uint32_t color(int owner) {return owner?0x5ba8ff:0xff6b7c;}
static lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y)
{
    lv_obj_t *a=lv_label_create(parent);lv_label_set_text(a,value);lv_obj_set_pos(a,x,y);
    lv_obj_set_style_text_color(a,lv_color_hex(0xe7edf8),0);return a;
}
static lv_obj_t *text_center(lv_obj_t *parent,const char *value,int x,int y,int w)
{
    lv_obj_t *a=text(parent,value,x,y);
    lv_obj_set_width(a,w);
    lv_obj_set_style_text_align(a,LV_TEXT_ALIGN_CENTER,0);
    return a;
}
static void status(const char *value) {lv_label_set_text(hint,value);}
static void layout_board(void)
{
    int size=game.config.size;
    int cell,x0=76,y0=86,x,y;
    if(size<GY_SIZE_MIN||size>GY_SIZE_MAX)size=6;
    cell=240/size;
    for(y=0;y<GY_MAX;++y)for(x=0;x<GY_MAX;++x){
        if(x<size&&y<size){
            lv_obj_remove_flag(tiles[y][x],LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(tiles[y][x],x0+x*cell,y0+y*cell);
            lv_obj_set_size(tiles[y][x],cell-3,cell-3);
            lv_obj_set_style_text_font(labels[y][x],
                size>=7?&lv_font_montserrat_14:&lv_font_montserrat_18,0);
            lv_obj_set_size(health[y][x],cell>20?cell-10:12,4);
            lv_obj_align(health[y][x],LV_ALIGN_BOTTOM_MID,0,-3);
        }else lv_obj_add_flag(tiles[y][x],LV_OBJ_FLAG_HIDDEN);
    }
}
static const char *net_phase_text(void)
{
    switch(gy_net_app_phase()){
    case GY_NET_PHASE_WAIT:return "等待准备";
    case GY_NET_PHASE_STARTING:return "3 秒后开始";
    case GY_NET_PHASE_PLAYING:return "对局中";
    case GY_NET_PHASE_RESULT:return "结算";
    default:return "已结束";
    }
}
/* 座位状态文案：在线看准备状态；离线区分本机断链、远端倒计时和已断线。 */
static void net_seat_state_text(char *buf,int cap,int color,int ready)
{
    int sec;
    if(gy_net_app_seat_online(color)){
        lv_snprintf(buf,cap,ready?"已准备":"未准备");
        return;
    }
    sec=gy_net_app_seat_reconnect_seconds(color);
    if(gy_net_app_role()==GY_NET_ROLE_SPECTATOR&&
       gy_net_app_reconnect_seconds()>0&&color==gy_net_app_host_color())
        lv_snprintf(buf,cap,"连接中断");
    else if(sec>0)lv_snprintf(buf,cap,"断线重连 %ds",sec);
    else lv_snprintf(buf,cap,"断线");
}
/* 重连横幅统一文案；host_text 由页面决定（房间页“等待对手”，对局页“对方断线”）。
 * overlay=1 时沿用对局页大字横幅的“%d s”格式。 */
static void net_reconn_text(char *buf,int cap,const char *host_text,int overlay)
{
    int sec=gy_net_app_remote_reconnect_seconds();
    int off=gy_net_app_offline_seat();
    if(gy_net_app_role()==GY_NET_ROLE_HOST)
        lv_snprintf(buf,cap,overlay?"%s 重连中 %d s":"%s %ds",host_text,sec);
    else if(gy_net_app_role()==GY_NET_ROLE_SPECTATOR&&off>=0&&
            gy_net_app_reconnect_seconds()==0)
        lv_snprintf(buf,cap,overlay?"%s断线 重连中 %d s":"%s断线 重连中 %ds",
                    off==GY_NET_COLOR_RED?"红方":"蓝方",sec);
    else
        lv_snprintf(buf,cap,overlay?"连接中断 重连中 %d s":"连接中断 重连中 %ds",sec);
}
static void net_lobby_refresh(void)
{
    int i;
    if(!net_lobby_status)return;
    for(i=0;i<5;++i){
        const GyNetAppRoom *r=gy_net_app_room_at(i);
        lv_obj_t *label=net_room_rows[i]?lv_obj_get_child(net_room_rows[i],0):NULL;
        if(!label)continue;
        if(r){
            char text[64];
            const char *phase=r->phase==GY_NET_PHASE_WAIT?"等待中":
                              r->phase==GY_NET_PHASE_STARTING?"即将开始":
                              r->phase==GY_NET_PHASE_PLAYING?"对战中":
                              r->phase==GY_NET_PHASE_RESULT?"结算中":"已关闭";
            int can_join=r->phase!=GY_NET_PHASE_RESULT&&r->phase!=GY_NET_PHASE_CLOSED;
            lv_snprintf(text,sizeof(text),"房间 %04x  %d/2  %s  观战 %d",
                        (unsigned)r->room_id,(int)r->players,phase,(int)r->spectators);
            lv_label_set_text(label,text);
            if(can_join)lv_obj_remove_state(net_room_rows[i],LV_STATE_DISABLED);
            else lv_obj_add_state(net_room_rows[i],LV_STATE_DISABLED);
        }else{
            lv_label_set_text(label,"空房间位");
            lv_obj_add_state(net_room_rows[i],LV_STATE_DISABLED);
        }
    }
    lv_label_set_text(net_lobby_status,
        gy_net_app_join_rejected()?
            (gy_net_app_join_reason()==GY_NET_JOIN_REASON_NOT_FOUND?"房间不存在":
             gy_net_app_join_reason()==GY_NET_JOIN_REASON_FULL?"房间已满":
             gy_net_app_join_reason()==GY_NET_JOIN_REASON_TIMEOUT?"加入超时":"无法加入"):
        gy_net_app_room_count()?"点击房间加入":"扫描中...");
}
static void net_room_refresh(void)
{
    char text[80];
    int role=gy_net_app_role();
    int phase=gy_net_app_phase();
    if(!net_room_status)return;
    {
        const char *fail=gy_net_app_join_reason()==GY_NET_JOIN_REASON_NOT_FOUND?"房间不存在":
                         gy_net_app_join_reason()==GY_NET_JOIN_REASON_FULL?"房间已满":
                         gy_net_app_join_reason()==GY_NET_JOIN_REASON_TIMEOUT?"加入超时":"加入失败";
        int reconn=gy_net_app_remote_reconnect_seconds();
        if(reconn>0)
            lv_snprintf(text,sizeof(text),"%s 重连中 %ds | 本机 #%04X",
                        role==GY_NET_ROLE_HOST?"对手断线":
                        role==GY_NET_ROLE_SPECTATOR?
                            (gy_net_app_reconnect_seconds()>0?"连接中断":"玩家断线"):
                        "连接中断",
                        reconn,(unsigned)gy_net_app_local_id());
        else
            lv_snprintf(text,sizeof(text),"%s | %s | 本机 #%04X",
                        gy_net_app_phase()==GY_NET_PHASE_CLOSED?"房间已关闭":
                        gy_net_app_spectator_lost()?"已离开旁观":
                        gy_net_app_join_rejected()||!gy_net_app_active()?fail:
                        phase==GY_NET_PHASE_STARTING?"即将开始":net_phase_text(),
                        gy_net_app_local_terminal()?"对方断线":
                        gy_net_app_connected()?"已连接":
                        role==GY_NET_ROLE_HOST?"等待对手":"连接中",
                        (unsigned)gy_net_app_local_id());
        lv_label_set_text(net_room_status,text);
    }
    {
        int hc=gy_net_app_host_color();
        int red_ready=gy_net_app_seat_ready(GY_NET_COLOR_RED);
        int blue_ready=gy_net_app_seat_ready(GY_NET_COLOR_BLUE);
        int red_occ=gy_net_app_seat_occupied(GY_NET_COLOR_RED);
        int blue_occ=gy_net_app_seat_occupied(GY_NET_COLOR_BLUE);
        /* 准备中的座位统一变白,未准备保持红/蓝；只由该座位自己的 ready
         * 决定,绝不因为对方准备/自己不是房主而整块变白。 */
        if(net_red_box)
            lv_obj_set_style_bg_color(net_red_box,
                lv_color_hex(!red_occ?0x4b5563:red_ready?0x35d07f:0xff6b7c),0);
        if(net_blue_box)
            lv_obj_set_style_bg_color(net_blue_box,
                lv_color_hex(!blue_occ?0x4b5563:blue_ready?0x35d07f:0x5ba8ff),0);
        if(net_red_name){
            if(!red_occ){
                lv_label_set_text(net_red_name,"点击交换");
                lv_label_set_text(net_red_state,"");
            }else{
                int id=gy_net_app_seat_id(GY_NET_COLOR_RED);
                if(id>0)
                    lv_snprintf(text,sizeof(text),"玩家#%04X%s",
                                (unsigned)id,hc==GY_NET_COLOR_RED?"(房主)":"");
                else
                    lv_snprintf(text,sizeof(text),"玩家%s",
                                hc==GY_NET_COLOR_RED?"(房主)":"");
                lv_label_set_text(net_red_name,text);
                net_seat_state_text(text,sizeof(text),GY_NET_COLOR_RED,red_ready);
                lv_label_set_text(net_red_state,text);
            }
            lv_obj_set_style_text_color(net_red_name,
                lv_color_hex(red_occ?0x142238:0xe7edf8),0);
            lv_obj_set_style_text_color(net_red_state,
                lv_color_hex(red_occ?0x142238:0xe7edf8),0);
        }
        if(net_blue_name){
            if(!blue_occ){
                lv_label_set_text(net_blue_name,"点击交换");
                lv_label_set_text(net_blue_state,"");
            }else{
                int id=gy_net_app_seat_id(GY_NET_COLOR_BLUE);
                if(id>0)
                    lv_snprintf(text,sizeof(text),"玩家#%04X%s",
                                (unsigned)id,hc==GY_NET_COLOR_BLUE?"(房主)":"");
                else
                    lv_snprintf(text,sizeof(text),"玩家%s",
                                hc==GY_NET_COLOR_BLUE?"(房主)":"");
                lv_label_set_text(net_blue_name,text);
                net_seat_state_text(text,sizeof(text),GY_NET_COLOR_BLUE,blue_ready);
                lv_label_set_text(net_blue_state,text);
            }
            lv_obj_set_style_text_color(net_blue_name,
                lv_color_hex(blue_occ?0x142238:0xe7edf8),0);
            lv_obj_set_style_text_color(net_blue_state,
                lv_color_hex(blue_occ?0x142238:0xe7edf8),0);
        }
    }
    if(net_spectator_label){
        lv_snprintf(text,sizeof(text),"观战 %d 人",gy_net_app_spectator_count());
        lv_label_set_text(net_spectator_label,text);
    }
    if(net_ready_button){
        lv_obj_t *l=lv_obj_get_child(net_ready_button,0);
        if(l)lv_label_set_text(l,gy_net_app_local_ready()?"取消准备":"准备");
        if(role==GY_NET_ROLE_SPECTATOR||!gy_net_app_active())
            lv_obj_add_flag(net_ready_button,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(net_ready_button,LV_OBJ_FLAG_HIDDEN);
        if(!gy_net_app_active()||role==GY_NET_ROLE_SPECTATOR||
           gy_net_app_player_count()<2||
           (phase!=GY_NET_PHASE_WAIT&&phase!=GY_NET_PHASE_STARTING))
            lv_obj_add_state(net_ready_button,LV_STATE_DISABLED);
        else lv_obj_remove_state(net_ready_button,LV_STATE_DISABLED);
    }
    if(net_leave_button){
        lv_obj_t *l=lv_obj_get_child(net_leave_button,0);
        if(l)lv_label_set_text(l,role==GY_NET_ROLE_SPECTATOR?"退出旁观":"退出房间");
        lv_obj_set_width(net_leave_button,
                         role==GY_NET_ROLE_SPECTATOR?358:170);
    }
    if(gy_net_app_remote_reconnect_seconds()>0&&gy_net_app_active()){
        if(net_spectator_label)lv_obj_add_flag(net_spectator_label,LV_OBJ_FLAG_HIDDEN);
        if(net_room_hint){
            net_reconn_text(text,sizeof(text),"等待对手重连",0);
            lv_label_set_text(net_room_hint,text);
            lv_obj_set_style_text_font(net_room_hint,&geyi_font_cn16,0);
            lv_obj_set_style_text_color(net_room_hint,lv_color_hex(0x142238),0);
            lv_obj_set_style_bg_color(net_room_hint,lv_color_hex(0xffdc79),0);
            lv_obj_set_style_bg_opa(net_room_hint,LV_OPA_COVER,0);
            lv_obj_set_style_radius(net_room_hint,8,0);
            lv_obj_set_style_pad_all(net_room_hint,6,0);
            lv_obj_set_pos(net_room_hint,16,292);
        }
    }else if(phase==GY_NET_PHASE_STARTING&&gy_net_app_active()){
        int sec=gy_net_app_countdown_seconds();
        if(net_spectator_label)lv_obj_add_flag(net_spectator_label,LV_OBJ_FLAG_HIDDEN);
        if(net_room_hint){
            /* 321 实时倒计时：黄底大字,比原来的小字明显。 */
            lv_snprintf(text,sizeof(text),"%d s",sec);
            lv_label_set_text(net_room_hint,text);
            lv_obj_set_style_text_font(net_room_hint,&lv_font_montserrat_24,0);
            lv_obj_set_style_text_color(net_room_hint,lv_color_hex(0x142238),0);
            lv_obj_set_style_bg_color(net_room_hint,lv_color_hex(0xffdc79),0);
            lv_obj_set_style_bg_opa(net_room_hint,LV_OPA_COVER,0);
            lv_obj_set_style_radius(net_room_hint,8,0);
            lv_obj_set_style_pad_all(net_room_hint,6,0);
            lv_obj_set_pos(net_room_hint,16,292);
        }
    }else{
        int can=gy_net_app_active()&&role==GY_NET_ROLE_HOST&&phase==GY_NET_PHASE_WAIT&&
                !gy_net_app_host_ready()&&!gy_net_app_guest_ready();
        if(net_spectator_label)lv_obj_remove_flag(net_spectator_label,LV_OBJ_FLAG_HIDDEN);
        /* 不再用 LV_STATE_DISABLED 修饰座位框：按钮主题会把禁用态画成
         * 灰白色,导致“只有自己准备,双方框都变色”。点击是否有效由
         * gy_net_app_swap() 自己判断。 */
        if(net_room_hint){
            lv_label_set_text(net_room_hint,can?"点击方框交换先后手(准备前)":
                              role==GY_NET_ROLE_SPECTATOR?"旁观中 只读":
                              "房主可在准备前交换先后手");
            lv_obj_set_style_text_font(net_room_hint,&geyi_font_cn16,0);
            lv_obj_set_style_text_color(net_room_hint,lv_color_hex(0xe7edf8),0);
            lv_obj_set_style_bg_opa(net_room_hint,LV_OPA_TRANSP,0);
            lv_obj_set_style_radius(net_room_hint,0,0);
            lv_obj_set_style_pad_all(net_room_hint,0,0);
            lv_obj_set_pos(net_room_hint,16,304);
        }
    }
}
static int net_result_shown=-1;
static const char *net_side_tag(int color)
{
    int rel=gy_net_app_side_relation(color);
    return rel==1?"(你)":rel==0?"(对方)":"";
}
/* 结算文案统一从“座位关系”生成，不能再用 local_turn()：
 * 它包含 phase/断线门控，RESULT 时恒为 0，会把本机写成“对方”。 */
static void net_result_text(char *buf,int cap,int result,const char *tail)
{
    const char *base=result==GY_RED_WIN?"红方胜":
                     result==GY_BLUE_WIN?"蓝方胜":
                     result==GY_DRAW?"平局":"对局结束";
    int side=result==GY_RED_WIN?GY_NET_COLOR_RED:
             result==GY_BLUE_WIN?GY_NET_COLOR_BLUE:GY_NET_COLOR_NONE;
    lv_snprintf(buf,cap,"%s%s%s",base,
                side==GY_NET_COLOR_NONE?"":net_side_tag(side),
                tail?tail:"");
}
static void net_result_show(int result)
{
    const char *title=(result==GY_RED_WIN)?"RED WIN":
                       (result==GY_BLUE_WIN)?"BLUE WIN":
                       (result==GY_DRAW)?"DRAW":"GAME OVER";
    char sub[80];
    int rel=result==GY_RED_WIN?gy_net_app_side_relation(GY_NET_COLOR_RED):
            result==GY_BLUE_WIN?gy_net_app_side_relation(GY_NET_COLOR_BLUE):-1;
    int key=result*4+(rel+1);
    net_result_text(sub,sizeof(sub),result,",即将返回房间");
    if(!net_result_overlay)return;
    if(net_result_shown==key &&
       !lv_obj_has_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN))return;
    net_result_shown=key;
    lv_label_set_text(net_result_title,title);
    lv_label_set_text(net_result_sub,sub);
    if(net_reconn_overlay)lv_obj_add_flag(net_reconn_overlay,LV_OBJ_FLAG_HIDDEN);
    net_confirm_hide();
    lv_obj_remove_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(net_result_overlay);
}
static void net_result_hide(void)
{
    net_result_shown=-1;
    if(net_result_overlay)lv_obj_add_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);
}
static void net_reconn_refresh(void)
{
    char text[80];
    int sec=gy_net_app_remote_reconnect_seconds();
    if(!net_reconn_overlay||!net_reconn_label)return;
    if(sec>0&&gy_net_app_active()&&!gy_net_app_local_terminal()){
        net_reconn_text(text,sizeof(text),"对方断线",1);
        lv_label_set_text(net_reconn_label,text);
        lv_obj_remove_flag(net_reconn_overlay,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(net_reconn_overlay);
    }else{
        lv_obj_add_flag(net_reconn_overlay,LV_OBJ_FLAG_HIDDEN);
    }
}
static void net_confirm_show(void)
{
    if(!net_confirm_overlay)return;
    lv_label_set_text(net_confirm_label,"认输会让对手获胜,确定?");
    if(net_result_overlay)lv_obj_add_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);
    if(net_reconn_overlay)lv_obj_add_flag(net_reconn_overlay,LV_OBJ_FLAG_HIDDEN);
    if(net_confirm_block)lv_obj_remove_flag(net_confirm_block,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(net_confirm_overlay,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(net_confirm_overlay);
}
static void net_confirm_hide(void)
{
    if(net_confirm_block)lv_obj_add_flag(net_confirm_block,LV_OBJ_FLAG_HIDDEN);
    if(net_confirm_overlay)lv_obj_add_flag(net_confirm_overlay,LV_OBJ_FLAG_HIDDEN);
}
static void render(void)
{
    const GyGame *shown=ready&&!placing?&preview.visible:&game;
    const char *outcomes[]={"","红方胜","蓝方胜","平局"};
    const char *dirs[]={"上","右","下","左"};
    int x,y,size=game.config.size;char value[80];
    int shown_result=gy_net_app_active()?gy_net_app_result():game.outcome;
    if(size<GY_SIZE_MIN||size>GY_SIZE_MAX)size=6;
    if(resign_button){
        lv_obj_t *l=lv_obj_get_child(resign_button,0);
        const char *lbl;
        if(gy_net_app_active())
            lbl=gy_net_app_is_spectator()?"退出旁观":
                (gy_net_app_phase()==GY_NET_PHASE_RESULT?"退出":"认输");
        else lbl=game.outcome?"返回首页":"认输";
        if(l)lv_label_set_text(l,lbl);
    }
    {
        int netres=gy_net_app_active()?gy_net_app_result():game.outcome;
        if(netres){
            net_result_text(value,sizeof(value),netres,"");
            lv_label_set_text(turn_label,value);
        }
        else if(gy_net_app_active()){
        int reconn=gy_net_app_reconnect_seconds();
        if(reconn>0){
            lv_snprintf(value,sizeof(value),"%s 重连中 %ds",
                gy_net_app_role()==GY_NET_ROLE_HOST?"对方断线":"连接中断",reconn);
            lv_label_set_text(turn_label,value);
        }
        else if(gy_net_app_local_terminal())lv_label_set_text(turn_label,"对方断线");
        else{
            lv_snprintf(value,sizeof(value),"%s回合%s",game.turn?"蓝方":"红方",
                gy_net_app_is_spectator()?"旁观":net_side_tag(game.turn));
            lv_label_set_text(turn_label,value);
        }
            }else if(ai_side>=0){
        lv_snprintf(value,sizeof(value),"%s回合%s",game.turn?"蓝方":"红方",game.turn==ai_side?"(AI)":"(你)");
        lv_label_set_text(turn_label,value);
    }else lv_label_set_text(turn_label,game.turn?"蓝方回合":"红方回合");
    }
    lv_obj_set_style_text_color(turn_label,lv_color_hex(shown_result==GY_DRAW?0xffdc79:
        color(shown_result==GY_RED_WIN?0:shown_result==GY_BLUE_WIN?1:game.turn)),0);
    lv_snprintf(value,sizeof(value),"红方 %d 金币    蓝方 %d 金币",game.coins[0],game.coins[1]);
    lv_label_set_text(money_label,value);
    {int displayed=ready&&!placing?selected_count:amount;
    lv_snprintf(value,sizeof(value),"%d 个",displayed);lv_label_set_text(count_label,value);}
    for(y=0;y<size;++y)for(x=0;x<size;++x) {
        const GyCell *c=&shown->cells[y][x];
        int changed=ready&&!placing&&memcmp(c,&game.cells[y][x],sizeof(*c));
        uint32_t bg=(x+y)%2?0x25344b:0x202e44;
        lv_obj_set_style_border_width(tiles[y][x],changed?2:0,0);
        lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(0x61e0c2),0);
        /* 只画真正可放的格子：外边沿行/列的第一个空格；不能再画整圈。 */
        if(placing&&gy_place_legal(&game,y,x)) {
            lv_obj_set_style_border_width(tiles[y][x],2,0);
            lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(0x65768f),0);
        }
        if(placing&&place_row==y&&place_column==x) {
            lv_obj_set_style_border_width(tiles[y][x],2,0);
            lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(0xffdc79),0);
        }
        lv_obj_add_flag(health[y][x],LV_OBJ_FLAG_HIDDEN);
        if(c->kind==GY_SOLDIER) {
            bg=color(c->owner);lv_snprintf(value,sizeof(value),"%c%d",c->owner?'B':'R',c->hp);
            lv_obj_remove_flag(health[y][x],LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_range(health[y][x],0,game.config.hp>0?game.config.hp:1);
            lv_bar_set_value(health[y][x],c->hp,LV_ANIM_OFF);
        } else if(c->kind==GY_NUMBER) {
            bg=0xc6a66b;lv_snprintf(value,sizeof(value),"%u",c->value);
        } else value[0]=0;
        lv_obj_set_style_bg_color(tiles[y][x],lv_color_hex(bg),0);
        lv_label_set_text(labels[y][x],value);
        lv_obj_set_style_text_color(labels[y][x],lv_color_hex(c->kind==GY_EMPTY?0xffffff:0x142238),0);
        lv_obj_align(labels[y][x],LV_ALIGN_CENTER,0,c->kind==GY_SOLDIER?-3:0);
    }
    if(gy_net_app_active()&&gy_net_app_is_spectator()){
        int side;
        for(side=0;side<2;++side){
            const GyNetPreview *np=gy_net_app_preview_at(side);
            GyMove nm;GyPreview nv;
            if(!gy_net_app_preview_visible(side)||!np)continue;
            nm.direction=np->direction;nm.count=np->count;nm.aim=np->aim;
            if(!gy_preview(&game,nm,&nv))continue;
            for(y=0;y<size;++y)for(x=0;x<size;++x)
                if(memcmp(&nv.visible.cells[y][x],&game.cells[y][x],sizeof(GyCell))){
                    lv_obj_set_style_border_width(tiles[y][x],2,0);
                    lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(side?0x5ba8ff:0xff6b7c),0);
                }
        }
    }
    if(game.outcome)status(gy_net_app_active()?"本局结束,即将返回房间":
                                           "本局结束,点右上角返回首页");
    else if(gy_net_app_active()){
        int reconn=gy_net_app_remote_reconnect_seconds();
        int netres=gy_net_app_result();
        if(netres){
            net_result_text(value,sizeof(value),netres,",即将返回房间");
            status(value);
        }
        else if(reconn>0){
            net_reconn_text(value,sizeof(value),"等待对手重连",0);
            status(value);
        }
        else if(gy_net_app_local_terminal())status("对方断线(本地提示,不是权威结算)");
        else if(gy_net_app_phase()==GY_NET_PHASE_STARTING)status("双方已准备 3 秒后开始");
        else if(gy_net_app_phase()!=GY_NET_PHASE_PLAYING)status(net_phase_text());
        else if(gy_net_app_is_spectator())status("旁观中 只读");
        else if(placing)status(place_column<0?"放置 请选择空格":"已选位置 请确认或取消");
        else if(ready){lv_snprintf(value,sizeof(value),"预览 %s %d 个 确认后发送",dirs[direction],selected_count);status(value);}
        else status(gy_net_app_local_turn()?"你的回合 先选方向":"对方回合 等待快照");
    }
    else if(ai_thinking)status("AI 思考中...");
    else if(ai_preview_pending)status("AI 预览 即将落子");
    else if(placing)status(place_column<0?"放置 请选择空格":"已选位置 请确认或取消");
    else if(ready) {
        lv_snprintf(value,sizeof(value),"预览 %s %d 个 确认后移动",dirs[direction],selected_count);status(value);
    }else if(ai_side>=0)status(game.turn==ai_side?"AI 回合":"先选方向 再确认");
    if(ready&&human_turn()&&!gy_net_app_is_spectator()&&!gy_net_app_pending_move())
        lv_obj_remove_state(confirm_button,LV_STATE_DISABLED);
    else lv_obj_add_state(confirm_button,LV_STATE_DISABLED);
}
static void start_ai(void)
{
    GyAiConfig cfg;
    if(ai_side<0||!playing||game.outcome!=GY_PLAYING||game.turn!=ai_side||
       ai_thinking||ai_preview_pending)return;
    cfg=gy_ai_default_config();
    {const GyParamSet *p=gy_param_mode(GY_MODE_PVE);
     if(p)cfg.max_depth=p->search_depth;}
    /* 5/6 层是用户明确要求的搜索上限；300ms 默认预算会在更深迭代前
     * 截断，导致串口只报告 depth=4。高深度使用节点上限兜底，不再用
     * 300ms 提前终止，保证 5/6 层设置能实际跑完。 */
    if(cfg.max_depth>=5)cfg.time_limit_ms=0;
    ++ai_generation;
    ai_revision=game.revision;
    if(gy_ai_task_start(&game,&cfg,ai_revision,ai_generation)==0)ai_thinking=1;
    render();
}
static void apply_ai(const GyAiAction *a)
{
    uint32_t old_revision=game.revision;
    int old_outcome=game.outcome;
    int ok=0;
    if(a->kind==GY_AI_MOVE){
        GyPreview p;
        if(gy_preview(&game,a->move,&p))ok=gy_commit(&game,&p);
    }else if(a->kind==GY_AI_PLACE)ok=gy_place(&game,a->column);
    ai_preview_pending=0;ai_pending.kind=0;
    if(!ok){render();start_ai();return;}
    reset_selection_ui();
    gy_nav_clear(&nav);
    if(!old_outcome&&game.outcome)
        feedback=game.outcome==GY_RED_WIN?GY_FX_RED:game.outcome==GY_BLUE_WIN?GY_FX_BLUE:GY_FX_DRAW;
    else if(game.revision!=old_revision)feedback=GY_FX_MOVE;
    render();
    if(!game.outcome&&game.turn==ai_side)start_ai();
}
static void begin_ai_preview(const GyAiAction *a)
{
    if(a->kind==GY_AI_MOVE){
        if(!gy_preview(&game,a->move,&preview)){apply_ai(a);return;}
        direction=a->move.direction;
        selected_count=a->move.count;
        amount=selected_count?selected_count:1;
        clear_ui_placement();ready=1;
    }else if(a->kind==GY_AI_PLACE){
        int row=game.turn==0?game.config.size-1:0;
        if(a->column<0||a->column>=game.config.size||
           game.cells[row][a->column].kind!=GY_EMPTY||
           game.coins[game.turn]<game.config.cost){apply_ai(a);return;}
        placing=1;ready=1;place_row=row;place_column=a->column;direction=-1;aim=0;
    }else{apply_ai(a);return;}
    gy_nav_clear(&nav);
    ai_pending=*a;
    ai_preview_pending=1;
    ai_preview_at=lv_tick_get();
    render();
}
static void commit_ai_pending(void)
{
    uint32_t old_revision=game.revision;
    int old_outcome=game.outcome;
    int ok=0;
    if(ai_pending.kind==GY_AI_MOVE)ok=gy_commit(&game,&preview);
    else if(ai_pending.kind==GY_AI_PLACE)ok=gy_place(&game,ai_pending.column);
    ai_preview_pending=0;ai_pending.kind=0;
    reset_selection_ui();
    gy_nav_clear(&nav);
    if(!ok){status("AI 动作失效 重新思考");render();start_ai();return;}
    if(!old_outcome&&game.outcome)
        feedback=game.outcome==GY_RED_WIN?GY_FX_RED:game.outcome==GY_BLUE_WIN?GY_FX_BLUE:GY_FX_DRAW;
    else if(game.revision!=old_revision)feedback=GY_FX_MOVE;
    render();
    if(!game.outcome&&game.turn==ai_side)start_ai();
}
static void choose(void)
{
    GyMove m;
    int available;
    ready=0;
    if(direction<0||placing||game.outcome)return;
    available=gy_eligible_count(&game,direction);
    m.direction=direction;m.count=available==0?0:(amount>available?available:amount);m.aim=0;
    selected_count=m.count;
    ready=gy_preview(&game,m,&preview);
    if(ready)gy_nav_button_selected(&nav);
    else{gy_nav_clear(&nav);status("方向不可用 请重新选择");}
}
static void param_message(const char *msg)
{
    lv_snprintf(param_status_text,sizeof(param_status_text),"%s",msg);
    if(param_status_label)lv_label_set_text(param_status_label,param_status_text);
}
static void param_refresh(void)
{
    int i,mode=gy_param_ui_mode();
    for(i=0;i<GY_MODE_COUNT;++i)if(param_mode_buttons[i])
        lv_obj_set_style_bg_color(param_mode_buttons[i],
            lv_color_hex(i==mode?0x167d73:0x344760),0);
    for(i=0;i<GY_PARAM_UI_ROWS;++i){
        int id=gy_param_ui_item_at(i);
        if(id<0){lv_obj_add_flag(param_row_obj[i],LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(param_row_obj[i],LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(param_row_name[i],gy_param_name(id));
        {int lo=0,hi=0;char value[40];
         gy_param_range(id,&lo,&hi);
         lv_snprintf(value,sizeof(value),"%d [%d-%d]",gy_param_ui_value(id),lo,hi);
         lv_label_set_text(param_row_value[i],value);}
    }
    {char value[16];
     lv_snprintf(value,sizeof(value),"%d/%d",gy_param_ui_page()+1,gy_param_ui_page_count());
     lv_label_set_text(param_page_label,value);}
    if(param_prev_button)
        lv_obj_set_style_opa(param_prev_button,gy_param_ui_page()==0?LV_OPA_40:LV_OPA_COVER,0);
    if(param_next_button)
        lv_obj_set_style_opa(param_next_button,
            gy_param_ui_page()>=gy_param_ui_page_count()-1?LV_OPA_40:LV_OPA_COVER,0);
    lv_label_set_text(param_status_label,param_status_text);
}
static void param_adjust_event(lv_event_t *e)
{
    int code=(int)(intptr_t)lv_event_get_user_data(e);
    int row=code>>1,delta=(code&1)?1:-1;
    int id=gy_param_ui_item_at(row),rc;
    if(id<0)return;
    rc=gy_param_ui_adjust(id,delta);
    if(rc==GY_PARAM_OK)param_message("OK");
    else if(rc==GY_PARAM_ERR_ODD)param_message("ODD -> CLAMPED");
    else if(rc==GY_PARAM_ERR_RANGE)param_message("CLAMPED");
    else param_message("ERROR");
    param_refresh();
}
static void choose_net(void)
{
    GyMove m;int available;
    if(direction<0||placing||game.outcome)return;
    available=gy_eligible_count(&game,direction);
    m.direction=direction;m.count=available==0?0:(amount>available?available:amount);m.aim=0;
    selected_count=m.count;
    ready=gy_preview(&game,m,&preview);
    if(!ready){gy_nav_clear(&nav);status("方向不可用 请重新选择");}
    else{gy_nav_button_selected(&nav);gy_net_app_preview(direction,selected_count,0);}
}
void gy_ui_action(int a)
{
    uint32_t old_revision=game.revision;
    int old_outcome=game.outcome;
#ifdef GEYI_WITH_RTTHREAD
    /* 不在 AI 搜索/预览期间写 Flash，避免 FlashDB 擦写拖住 UI 线程。 */
    if(!ai_thinking&&!ai_preview_pending)gy_history_store_flush(lv_tick_get());
#endif
    history_net_tick(lv_tick_get());
    if(a==GY_UI_NET_CONFIRM_NO){
        net_confirm_hide();return;
    }
    if(a==GY_UI_NET_CONFIRM_YES){
        net_confirm_hide();
        if(!gy_net_app_active()||gy_net_app_phase()!=GY_NET_PHASE_PLAYING){
            /* RESULT 之后的迟到确认不能再发 RESIGN/释放席位。 */
            render();return;
        }
        /* 认输即结束：先清本地预览，避免结果快照到达前还渲染旧预览盘面。 */
        reset_selection_ui();gy_nav_clear(&nav);
        gy_net_app_resign();
        render();
        return;
    }
    if(a==GY_UI_NET){
        ai_side=-1;feedback=GY_FX_CLEAR;
        gy_net_app_lobby_start();show_page(net_lobby_page);net_lobby_refresh();return;
    }
    if(a==GY_UI_NET_CREATE){
        gy_net_app_create_room(0);show_page(net_room_page);net_room_refresh();return;
    }
    if(a==GY_UI_NET_BACK){
        gy_net_app_lobby_stop();gy_net_app_leave();show_page(mode_page);return;
    }
    if(a>=GY_UI_NET_JOIN0&&a<=GY_UI_NET_JOIN4){
        int idx=a-GY_UI_NET_JOIN0;
        if(gy_net_app_join_index(idx)==0){show_page(net_room_page);net_room_refresh();}
        else net_lobby_refresh();
        return;
    }
    if(a==GY_UI_NET_READY){gy_net_app_set_ready(!gy_net_app_local_ready());net_room_refresh();return;}
    if(a==GY_UI_NET_SWAP){gy_net_app_swap();net_room_refresh();return;}
    if(a==GY_UI_NET_LEAVE){
        gy_net_app_leave();gy_net_app_lobby_start();show_page(net_lobby_page);return;
    }
    if(a==GY_UI_PARAMS){
        ai_side=-1;feedback=GY_FX_CLEAR;show_page(param_page);
        param_message("新开一局生效");param_refresh();return;
    }
    if(a==GY_UI_PARAM_BACK){show_page(home_page);return;}
    if(a==GY_UI_PARAM_PREV){if(gy_param_ui_page_prev()){param_message("新开一局生效");param_refresh();}return;}
    if(a==GY_UI_PARAM_NEXT){if(gy_param_ui_page_next()){param_message("新开一局生效");param_refresh();}return;}
    if(a==GY_UI_PARAM_RESET){
        gy_param_ui_reset();param_message("已重置");param_refresh();return;
    }
    if(a==GY_UI_PARAM_MODE_SINGLE||a==GY_UI_PARAM_MODE_PVE||a==GY_UI_PARAM_MODE_PVP){
        int mode=a==GY_UI_PARAM_MODE_SINGLE?GY_MODE_SINGLE:
                 a==GY_UI_PARAM_MODE_PVE?GY_MODE_PVE:GY_MODE_PVP;
        gy_param_ui_set_mode(mode);
        param_message(mode==GY_MODE_PVP?"联机参数由房主使用":"新开一局生效");
        param_refresh();return;
    }
    if(a==GY_UI_RULES){
        rules_index=0;show_page(rules_page);rules_refresh();return;
    }
    if(a==GY_UI_RULES_BACK){show_page(home_page);return;}
    if(a==GY_UI_RULES_PREV){
        if(rules_index>0){--rules_index;rules_refresh();}return;
    }
    if(a==GY_UI_RULES_NEXT){
        if(rules_index+1<RULES_PAGES){++rules_index;rules_refresh();}return;
    }
    if(a==GY_UI_HOME){
        if(gy_net_app_active())gy_net_app_leave();
        gy_net_app_lobby_stop();
        ai_side=-1;feedback=GY_FX_CLEAR;gy_nav_clear(&nav);
        show_page(home_page);return;
    }
    if(a==GY_UI_MODES){
        if(gy_net_app_active())gy_net_app_leave();
        gy_net_app_lobby_stop();
        ai_side=-1;feedback=GY_FX_CLEAR;gy_nav_clear(&nav);
        show_page(mode_page);return;
    }
    if(a==GY_UI_SINGLE){
        if(gy_net_app_active())gy_net_app_leave();
        gy_net_app_lobby_stop();
        ai_side=-1;game_mode=GY_MODE_SINGLE;show_page(game_page);a=GY_UI_RESTART;
    }
    if(a==GY_UI_AI){
        if(gy_net_app_active())gy_net_app_leave();
        gy_net_app_lobby_stop();
        ai_side=-1;prep_human_red=1;update_prep_labels();
        feedback=GY_FX_CLEAR;gy_nav_clear(&nav);
        show_page(prep_page);return;
    }
    if(a==GY_UI_PREP_SWAP){
        if(!prep_active)return;
        prep_human_red=!prep_human_red;update_prep_labels();return;
    }
    if(a==GY_UI_PREP_CANCEL){ai_side=-1;feedback=GY_FX_CLEAR;gy_nav_clear(&nav);show_page(mode_page);return;}
    if(a==GY_UI_PREP_START){
        ai_side=prep_human_red?1:0;game_mode=GY_MODE_PVE;show_page(game_page);a=GY_UI_RESTART;
    }
    if(a==GY_UI_HISTORY){
        feedback=GY_FX_CLEAR;
        if(gy_net_app_active())gy_net_app_leave();
        gy_net_app_lobby_stop();
        ai_side=-1;gy_nav_clear(&nav);
        hist_index=0;history_reset_confirm();history_refresh();
        show_page(history_page);return;
    }
    if(a==GY_UI_HISTORY_BACK){history_reset_confirm();history_refresh();show_page(history_page);return;}
    if(a==GY_UI_HISTORY_PREV){
        if(hist_index>0)--hist_index;
        history_reset_confirm();history_refresh();return;
    }
    if(a==GY_UI_HISTORY_NEXT){
        if((hist_index+1)*5<gy_history_count())++hist_index;
        history_reset_confirm();history_refresh();return;
    }
    if(a==GY_UI_HISTORY_CLEAR){
        if(!hist_confirm_clear){
            hist_confirm_clear=1;
            if(hist_clear_label)lv_label_set_text(hist_clear_label,"确认清空");
        }else{
            gy_history_clear();hist_index=0;history_reset_confirm();
        }
        history_refresh();return;
    }
    if(a==GY_UI_RESIGN&&!gy_net_app_active()){
        if(game.outcome){ai_side=-1;feedback=GY_FX_CLEAR;gy_nav_clear(&nav);show_page(home_page);}
        else if(playing){
            int resigner=game.turn;
            /* 人机模式只有人能点认输：即使当前轮到 AI，也由人认输、判人负。
             * 单机模式仍由当前回合方认输。 */
            if(ai_side>=0)resigner=ai_side==0?1:0;
            cancel_ai();
            match_resigner=resigner;
            if(game.turn!=resigner)game.turn=resigner;
            gy_resign(&game);reset_selection_ui();
            gy_nav_clear(&nav);
            if(game.outcome)feedback=game.outcome==GY_RED_WIN?GY_FX_RED:
                game.outcome==GY_BLUE_WIN?GY_FX_BLUE:GY_FX_DRAW;
            history_record_if_finished();
            render();
        }
        return;
    }
    if(!playing)return;
    /* 倾斜预览中：屏幕取消/增减/方向/放兵不生效，避免与倾斜状态互相覆盖；
     * 屏幕确认仍等价于 KEY2（REQ-319）。 */
    if(gy_nav_blocks_screen_edit(&nav,placing)&&
       (a==GY_UI_CANCEL||a<=GY_UI_LEFT||a==GY_UI_LESS||a==GY_UI_MORE||a==GY_UI_PLACE)){
        render();return;
    }
    if(gy_net_app_active()&&net_game_active){
        if(a==GY_UI_RESIGN){
            if(gy_net_app_is_spectator()){
                gy_net_app_leave();gy_net_app_lobby_start();show_page(net_lobby_page);return;
            }
            if(gy_net_app_phase()==GY_NET_PHASE_PLAYING)net_confirm_show();
            else {
                /* 结算/等待阶段右上“退出”先回房间页，不主动断链；
                 * 房间页的“离开”才是明确退出。 */
                show_page(net_room_page);net_room_refresh();
            }
            return;
        }
        if(!human_turn()||gy_net_app_is_spectator()){render();return;}
        if(a==GY_UI_CANCEL){
            ready=0;placing=0;gy_nav_clear(&nav);
            gy_net_app_cancel();status("已取消");render();return;
        }
        if(a<=GY_UI_LEFT){placing=0;direction=a;amount=1;choose_net();render();return;}
        if(a==GY_UI_LESS||a==GY_UI_MORE){
            amount+=a==GY_UI_MORE?1:-1;if(amount<1)amount=1;if(amount>36)amount=36;
            choose_net();render();return;
        }
        if(a==GY_UI_CONFIRM){
            if(!ready){status("没有可确认的选择");render();return;}
            if(!gy_net_app_confirm()){
                /* 可重试失败（pending/peer offline/host busy）保留预览，
                 * 不能一按就清空，否则松开后只能靠 200ms 重预览。 */
                status("发送失败 请等待同步或重试");render();return;
            }
            reset_selection_ui();gy_nav_clear(&nav);
            status("已发送 等待房主快照");render();return;
        }
        if(a==GY_UI_PLACE){
            clear_ui_placement();ready=0;gy_nav_clear(&nav);
            if(game.coins[game.turn]>=game.config.cost){
                gy_net_app_cancel();
                /* 链路短暂不可用时 net app 不会进入放兵；UI 不能单方面
                 * placing=1，否则重连后确认会被路由成移动请求。 */
                if(gy_net_app_place(-1,-1))placing=1;
                else status("网络未就绪 请稍后重试");
            }else{char msg[48];lv_snprintf(msg,sizeof(msg),"需要%d金币才能放置",game.config.cost);status(msg);}
            render();return;
        }
    }
    if(a==GY_UI_RESTART) {
        GyConfig config;
        cancel_ai();
        feedback=GY_FX_CLEAR;
        if(!gy_param_apply(game_mode,&config))config=gy_default_config();
        if(!gy_init(&game,config,20261015))gy_init(&game,gy_default_config(),20261015);
        layout_board();
        ++ui_session;
        amount=1;reset_selection_ui();
        gy_nav_clear(&nav);
        gy_timer_start(&match_timer,lv_tick_get());
        match_recorded=0;match_resigner=-1;
        history_reset_confirm();
        status("先选方向 再确认");
    } else if(ai_thinking||!human_turn()) {
        render();return;
    } else if(a==GY_UI_CANCEL) {
        reset_selection_ui();
        gy_nav_clear(&nav);
        status("已取消 棋盘状态不变");
    } else if(game.outcome) {render();return;}
    else if(a<=GY_UI_LEFT) {placing=0;direction=a;choose();}
    else if(a==GY_UI_LESS||a==GY_UI_MORE) {
        amount+=a==GY_UI_MORE?1:-1;if(amount<1)amount=1;if(amount>36)amount=36;choose();
    } else if(a==GY_UI_CONFIRM) {
        int ok=ready&&(placing?gy_place_at(&game,place_row,place_column):gy_commit(&game,&preview));
        reset_selection_ui();gy_nav_clear(&nav);
        status(ok?"回合结束 请交给对方":"没有可确认的选择");
    } else if(a==GY_UI_PLACE) {
        clear_ui_placement();ready=0;gy_nav_clear(&nav);
        if(game.coins[game.turn]>=game.config.cost)placing=1;
        else {char msg[48];lv_snprintf(msg,sizeof(msg),"需要%d金币才能放置",game.config.cost);status(msg);}
    }
    if(!old_outcome&&game.outcome)
        feedback=game.outcome==GY_RED_WIN?GY_FX_RED:game.outcome==GY_BLUE_WIN?GY_FX_BLUE:GY_FX_DRAW;
    else if(a==GY_UI_CONFIRM&&game.revision!=old_revision)feedback=GY_FX_MOVE;
    history_record_if_finished();
    render();
    if(!game.outcome&&ai_side>=0&&game.turn==ai_side)start_ai();
}
void gy_ui_cell(int x,int y)
{
    int size=game.config.size;
    if(size<GY_SIZE_MIN||size>GY_SIZE_MAX)size=6;
    if(gy_net_app_active()&&net_game_active){
        if(!human_turn()||gy_net_app_is_spectator()||!placing||game.outcome||
           x<0||x>=size||y<0||y>=size)return;
        {
            int legal=gy_place_legal(&game,y,x);
            place_row=y;place_column=x;
            /* 只有 net app 真正接受放置时才让 UI ready，避免断线窗口里
             * UI 有预览而 net app 的 placing 仍为 0，确认时错发 MOVE_REQ。 */
            ready=legal&&gy_net_app_place(y,x);
            render();
            if(!legal)status("该位置不可放置 请选择外围空格");
            else if(!ready)status("网络未就绪 请稍后重试");
        }
        return;
    }
    if(!playing||ai_thinking||!human_turn()||!placing||game.outcome||
       x<0||x>=size||y<0||y>=size)return;
    place_row=y;place_column=x;ready=gy_place_legal(&game,y,x);render();
    if(!ready)status("该位置不可放置 请选择外围空格");
}
static void action_event(lv_event_t *e) {gy_ui_action((int)(intptr_t)lv_event_get_user_data(e));}
static void cell_event(lv_event_t *e)
{int n=(int)(intptr_t)lv_event_get_user_data(e);gy_ui_cell(n%GY_MAX,n/GY_MAX);}
static lv_obj_t *button(lv_obj_t *root,const char *name,int x,int y,int w,int h,int action)
{
    lv_obj_t *b=lv_button_create(root);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_set_style_bg_color(b,lv_color_hex(action==GY_UI_CONFIRM?0x167d73:0x344760),0);
    lv_obj_set_style_radius(b,7,0);lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_t *label=lv_label_create(b);lv_label_set_text(label,name);lv_obj_center(label);
    lv_obj_add_event_cb(b,action_event,LV_EVENT_CLICKED,(void*)(intptr_t)action);return b;
}
static lv_obj_t *icon_part(lv_obj_t *parent,int x,int y,int w,int h,int radius,uint32_t bg)
{
    lv_obj_t *o=lv_obj_create(parent);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_radius(o,radius,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(bg),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static lv_obj_t *icon_group(lv_obj_t *parent,int x,int y)
{
    lv_obj_t *g=lv_obj_create(parent);
    lv_obj_set_pos(g,x,y);lv_obj_set_size(g,64,64);
    lv_obj_set_style_bg_opa(g,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(g,0,0);
    lv_obj_set_style_pad_all(g,0,0);
    lv_obj_remove_flag(g,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g,LV_OBJ_FLAG_CLICKABLE);
    return g;
}
static lv_obj_t *person_icon(lv_obj_t *parent,int x,int y)
{
    lv_obj_t *g=icon_group(parent,x,y);
    icon_part(g,22,6,20,20,LV_RADIUS_CIRCLE,0x142238);
    icon_part(g,12,32,40,24,12,0x142238);
    return g;
}
static lv_obj_t *ai_icon(lv_obj_t *parent,int x,int y)
{
    lv_obj_t *g=icon_group(parent,x,y);
    icon_part(g,29,9,6,6,LV_RADIUS_CIRCLE,0x142238);
    icon_part(g,31,15,2,8,0,0x142238);
    icon_part(g,12,23,40,30,8,0x142238);
    icon_part(g,21,32,7,7,LV_RADIUS_CIRCLE,0xffffff);
    icon_part(g,36,32,7,7,LV_RADIUS_CIRCLE,0xffffff);
    icon_part(g,24,44,16,3,1,0xffffff);
    return g;
}
static lv_obj_t *side_box(lv_obj_t *root,int x,int y,int w,int h,
                          int action,uint32_t bg,lv_obj_t **label_out,
                          lv_obj_t **player_out,lv_obj_t **ai_out)
{
    lv_obj_t *b=lv_button_create(root);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_set_style_bg_color(b,lv_color_hex(bg),0);
    lv_obj_set_style_radius(b,10,0);lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_set_style_pad_all(b,0,0);lv_obj_set_style_border_width(b,0,0);
    lv_obj_t *label=lv_label_create(b);
    lv_obj_set_width(label,w);
    lv_obj_align(label,LV_ALIGN_TOP_MID,0,112);
    lv_label_set_text(label,"玩家");
    lv_obj_set_style_text_color(label,lv_color_hex(0x142238),0);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *player=person_icon(b,0,0);
    lv_obj_t *ai=NULL;
    lv_obj_align(player,LV_ALIGN_TOP_MID,0,38);
    if(ai_out){
        ai=ai_icon(b,0,0);
        lv_obj_align(ai,LV_ALIGN_TOP_MID,0,38);
    }
    lv_obj_add_event_cb(b,action_event,LV_EVENT_CLICKED,(void*)(intptr_t)action);
    if(label_out)*label_out=label;
    if(player_out)*player_out=player;
    if(ai_out)*ai_out=ai;
    return b;
}
static lv_obj_t *net_seat_box(lv_obj_t *root,int x,int y,int w,int h,int action,
                              lv_obj_t **name_out,lv_obj_t **state_out)
{
    lv_obj_t *b=lv_button_create(root);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x4b5563),0);
    lv_obj_set_style_radius(b,10,0);lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_set_style_pad_all(b,0,0);lv_obj_set_style_border_width(b,0,0);
    lv_obj_t *icon=person_icon(b,0,0);
    lv_obj_align(icon,LV_ALIGN_TOP_MID,0,22);
    lv_obj_t *name=lv_label_create(b);
    lv_obj_set_pos(name,6,98);lv_obj_set_width(name,w-12);
    lv_obj_set_style_text_color(name,lv_color_hex(0xe7edf8),0);
    lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(name,&geyi_font_cn16,0);
    lv_obj_remove_flag(name,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *state=lv_label_create(b);
    lv_obj_set_width(state,w-16);
    lv_obj_align(state,LV_ALIGN_BOTTOM_RIGHT,-8,-8);
    lv_obj_set_style_text_color(state,lv_color_hex(0xe7edf8),0);
    lv_obj_set_style_text_align(state,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_remove_flag(state,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b,action_event,LV_EVENT_CLICKED,(void*)(intptr_t)action);
    if(name_out)*name_out=name;
    if(state_out)*state_out=state;
    return b;
}
static lv_obj_t *page(lv_obj_t *root)
{
    lv_obj_t *p=lv_obj_create(root);lv_obj_set_size(p,390,450);
    lv_obj_set_style_pad_all(p,0,0);lv_obj_set_style_border_width(p,0,0);
    lv_obj_set_style_bg_opa(p,LV_OPA_TRANSP,0);
    lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);return p;
}
static const char *history_winner_text(const GyRecord *r)
{
    return r->outcome==GY_RED_WIN?"红方":r->outcome==GY_BLUE_WIN?"蓝方":"平局";
}
static const char *history_mode_text(int mode)
{
    return mode==GY_MODE_PVE?"人机":mode==GY_MODE_PVP?"联机":"单机";
}
static const char *history_identity_text(int identity)
{
    switch(identity){
    case GY_IDENTITY_AI:return "AI";
    case GY_IDENTITY_HUMAN:return "真人";
    case GY_IDENTITY_OWNER:return "机主";
    case GY_IDENTITY_OPPONENT:return "对手";
    default:return "";
    }
}
static const char *history_identity_detail_text(int identity)
{
    const char *text=history_identity_text(identity);
    return *text?text:"无";
}
static const char *history_ended_text(int ended_by)
{
    return ended_by==GY_END_RED_RESIGN?"红方认输":
           ended_by==GY_END_BLUE_RESIGN?"蓝方认输":
           ended_by==GY_END_DISCONNECT?"断线":"全灭";
}
static void history_reset_confirm(void)
{
    hist_confirm_clear=0;
    if(hist_clear_label)lv_label_set_text(hist_clear_label,"清空");
}
static void history_refresh(void)
{
    char duration[8];
    int i,total=gy_history_count(),pages=(total+4)/5;
    if(pages<1)pages=1;
    if(hist_index>pages-1)hist_index=pages-1;
    if(hist_index<0)hist_index=0;
    if(hist_empty_label)lv_label_set_text(hist_empty_label,total?"":"暂无对局");
    for(i=0;i<5;++i){
        int index=hist_index*5+i;
        if(index<total&&index>=0){
            const GyRecord *r=gy_history_at(index);
            gy_timer_format(r->duration_ms,duration,(int)sizeof(duration));
            lv_label_set_text(hist_row_cells[i][0],history_mode_text(r->mode));
            lv_label_set_text(hist_row_cells[i][1],duration);
            lv_label_set_text(hist_row_cells[i][2],history_winner_text(r));
            lv_label_set_text(hist_row_cells[i][3],history_identity_text(r->winner_identity));
            lv_obj_remove_flag(hist_rows[i],LV_OBJ_FLAG_HIDDEN);
        }else lv_obj_add_flag(hist_rows[i],LV_OBJ_FLAG_HIDDEN);
    }
}
static void history_detail_open(int index)
{
    const GyRecord *r=gy_history_at(index);
    char value[48],duration[8],clock_text[8];
    if(!r)return;
    gy_timer_format(r->duration_ms,duration,(int)sizeof(duration));
    gy_timer_format(r->ts_sec*1000u,clock_text,(int)sizeof(clock_text));
    lv_snprintf(value,sizeof(value),"类型  %s",history_mode_text(r->mode));
    lv_label_set_text(hist_detail_lines[0],value);
    lv_snprintf(value,sizeof(value),"胜方  %s",history_winner_text(r));
    lv_label_set_text(hist_detail_lines[1],value);
    lv_snprintf(value,sizeof(value),"胜方身份  %s",history_identity_detail_text(r->winner_identity));
    lv_label_set_text(hist_detail_lines[2],value);
    lv_snprintf(value,sizeof(value),"棋盘  %u x %u",(unsigned)r->size,(unsigned)r->size);
    lv_label_set_text(hist_detail_lines[3],value);
    lv_snprintf(value,sizeof(value),"步数  %u",(unsigned)r->moves);
    lv_label_set_text(hist_detail_lines[4],value);
    lv_snprintf(value,sizeof(value),"时长  %s",duration);
    lv_label_set_text(hist_detail_lines[5],value);
    lv_snprintf(value,sizeof(value),"时间  开机%s",clock_text);
    lv_label_set_text(hist_detail_lines[6],value);
    show_page(history_detail_page);
}
static void history_row_event(lv_event_t *e)
{
    int row=(int)(intptr_t)lv_event_get_user_data(e);
    history_detail_open(hist_index*5+row);
}
/* 终局后写一条记录；由 tick 与动作处理共同调用，match_recorded 保证只写一次（REQ-602）。 */
static void history_record_if_finished(void)
{
    GyRecord record;
    if(!playing||match_recorded||game.outcome==GY_PLAYING)return;
    if(gy_net_app_active())return;   /* 联机对局走独立的网络状态机，本版不记 */
    match_recorded=1;
    gy_timer_stop(&match_timer,lv_tick_get());
    memset(&record,0,sizeof(record));
    record.mode=(uint8_t)game_mode;
    record.size=(uint8_t)game.config.size;
    record.outcome=(uint8_t)game.outcome;
    record.winner=(uint8_t)(game.outcome==GY_RED_WIN?0u:game.outcome==GY_BLUE_WIN?1u:2u);
    record.ended_by=(uint8_t)(match_resigner==0?GY_END_RED_RESIGN:
                              match_resigner==1?GY_END_BLUE_RESIGN:GY_END_KNOCKOUT);
    record.winner_identity=(uint8_t)(game_mode==GY_MODE_PVE?
                                     gy_history_identity_pve(game.outcome,ai_side):
                                     GY_IDENTITY_NONE);
    record.moves=(uint16_t)(game.moves>0xFFFFu?0xFFFFu:game.moves);
    record.duration_ms=gy_timer_elapsed(&match_timer,lv_tick_get());
    record.ts_sec=lv_tick_get()/1000u;
    gy_history_add(&record);
}
/* 联机结算按本机视角写一条；房主和客方走同一入口。
 * 必须在 gy_net_app_tick() 之后调用，且同时从 gy_ui_action() 进入，
 * 避免玩家在结果页立即离开时错过最后一条记录。 */
static void history_net_tick(uint32_t now)
{
    const GyGame *ng;
    GyRecord record;
    int phase,outcome,reason;
    uint8_t identity;

    if(!gy_net_app_active()){
        net_match_recorded=0;
        if(gy_timer_running(&net_match_timer))gy_timer_stop(&net_match_timer,now);
        return;
    }
    if(gy_net_app_is_spectator())return;

    phase=gy_net_app_phase();
    if(phase==GY_NET_PHASE_PLAYING){
        if(!gy_timer_running(&net_match_timer)){
            gy_timer_start(&net_match_timer,now);
            net_match_recorded=0;
        }
        return;
    }
    if(phase==GY_NET_PHASE_WAIT||phase==GY_NET_PHASE_CLOSED){
        if(gy_timer_running(&net_match_timer))gy_timer_stop(&net_match_timer,now);
        return;
    }
    if(phase!=GY_NET_PHASE_RESULT||net_match_recorded)return;

    ng=gy_net_app_game();
    outcome=gy_net_app_result();
    if(!outcome&&ng)outcome=ng->outcome;
    if(outcome<GY_RED_WIN||outcome>GY_DRAW)return;
    if(!ng||ng->config.size<GY_SIZE_MIN||ng->config.size>GY_SIZE_MAX)return;

    net_match_recorded=1;
    if(gy_timer_running(&net_match_timer))gy_timer_stop(&net_match_timer,now);
    identity=gy_history_identity_pvp(outcome,gy_net_app_local_color());
    reason=gy_net_app_result_reason();

    memset(&record,0,sizeof(record));
    record.mode=GY_MODE_PVP;
    record.size=(uint8_t)ng->config.size;
    record.outcome=(uint8_t)outcome;
    record.winner=(uint8_t)(outcome==GY_RED_WIN?0u:outcome==GY_BLUE_WIN?1u:2u);
    record.winner_identity=identity;
    if(reason==GY_NET_REASON_RESIGN)
        record.ended_by=(uint8_t)gy_history_ended_by_resign(outcome);
    else if(reason==GY_NET_REASON_NORMAL)
        record.ended_by=GY_END_KNOCKOUT;
    else
        record.ended_by=GY_END_DISCONNECT;
    record.moves=(uint16_t)(ng->moves>0xFFFFu?0xFFFFu:ng->moves);
    record.duration_ms=gy_timer_elapsed(&net_match_timer,now);
    record.ts_sec=now/1000u;
    gy_history_add(&record);
}
void gy_ui_create(void)
{
    int x,y,row;lv_obj_t *root=lv_screen_active();
    lv_obj_set_style_bg_color(root,lv_color_hex(0x111e31),0);
    lv_obj_set_style_text_font(root,&geyi_font_cn16,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    home_page=page(root);mode_page=page(root);game_page=page(root);prep_page=page(root);param_page=page(root);
    rules_page=page(root);
    net_lobby_page=page(root);net_room_page=page(root);
    text(home_page,"格弈",32,34);
    text(home_page,"倾斜棋盘  轮流对战",32,76);
    button(home_page,"开始游戏",32,120,326,52,GY_UI_MODES);
    button(home_page,"自定义游戏参数",32,186,326,48,GY_UI_PARAMS);
    button(home_page,"历史记录",32,244,326,48,GY_UI_HISTORY);
    button(home_page,"游戏规则",32,300,326,46,GY_UI_RULES);
    text(home_page,"单机 / 人机 / 联机",32,392);
    text(rules_page,"游戏规则",28,22);
    rules_page_label=text(rules_page,"1/4",302,22);
    lv_obj_set_width(rules_page_label,60);
    lv_obj_set_style_text_align(rules_page_label,LV_TEXT_ALIGN_RIGHT,0);
    rules_content=text(rules_page,"",28,58);
    lv_obj_set_width(rules_content,334);
    lv_label_set_long_mode(rules_content,LV_LABEL_LONG_MODE_WRAP);
    button(rules_page,"返回",20,378,88,40,GY_UI_RULES_BACK);
    button(rules_page,"上一页",118,378,96,40,GY_UI_RULES_PREV);
    button(rules_page,"下一页",256,378,96,40,GY_UI_RULES_NEXT);
    history_page=page(root);history_detail_page=page(root);
    text(history_page,"历史记录",28,22);
    hist_header[0]=text(history_page,"类型",40,52);
    hist_header[1]=text(history_page,"时长",100,52);
    hist_header[2]=text(history_page,"胜方",170,52);
    hist_header[3]=text(history_page,"胜方身份",230,52);
    lv_obj_set_width(hist_header[0],60);
    lv_obj_set_width(hist_header[1],70);
    lv_obj_set_width(hist_header[2],60);
    lv_obj_set_width(hist_header[3],120);
    hist_empty_label=text(history_page,"暂无对局",32,120);
    for(row=0;row<5;++row){
        lv_obj_t *b=lv_button_create(history_page);
        int col;
        lv_obj_set_pos(b,32,76+row*50);lv_obj_set_size(b,326,44);
        lv_obj_set_style_pad_all(b,0,0);lv_obj_set_style_border_width(b,0,0);
        lv_obj_set_style_bg_color(b,lv_color_hex(0x344760),0);
        lv_obj_set_style_radius(b,8,0);lv_obj_set_style_shadow_width(b,0,0);
        hist_rows[row]=b;
        for(col=0;col<4;++col){
            static const int x[4]={8,68,138,198};
            static const int w[4]={60,70,60,120};
            hist_row_cells[row][col]=lv_label_create(b);
            lv_label_set_text(hist_row_cells[row][col],"");
            lv_obj_set_width(hist_row_cells[row][col],w[col]);
            lv_obj_set_style_text_align(hist_row_cells[row][col],LV_TEXT_ALIGN_LEFT,0);
            lv_obj_align(hist_row_cells[row][col],LV_ALIGN_LEFT_MID,x[col],0);
        }
        lv_obj_add_event_cb(b,history_row_event,LV_EVENT_CLICKED,(void*)(intptr_t)row);
    }
    button(history_page,"上一页",32,340,100,42,GY_UI_HISTORY_PREV);
    button(history_page,"下一页",136,340,100,42,GY_UI_HISTORY_NEXT);
    hist_clear_button=button(history_page,"清空",240,340,118,42,GY_UI_HISTORY_CLEAR);
    hist_clear_label=lv_obj_get_child(hist_clear_button,0);
    button(history_page,"返回",32,392,326,40,GY_UI_HOME);
    text(history_detail_page,"对局详情",28,22);
    for(row=0;row<7;++row)hist_detail_lines[row]=text(history_detail_page,"",32,64+row*40);
    button(history_detail_page,"返回列表",32,388,150,42,GY_UI_HISTORY_BACK);
    button(history_detail_page,"首页",196,388,162,42,GY_UI_HOME);
    text(mode_page,"选择游戏模式",32,36);
    text(mode_page,"单机、人机或联机,开始一局对战",32,70);
    button(mode_page,"单机对战",32,104,326,44,GY_UI_SINGLE);
    text(mode_page,"两位玩家轮流操作一块开发板",40,152);
    button(mode_page,"人机对战",32,178,326,44,GY_UI_AI);
    text(mode_page,"进入后选择先后手,AI 自动走子",40,228);
    button(mode_page,"联机对战",32,260,326,44,GY_UI_NET);
    text(mode_page,"BLE 大厅 / 双人 / 旁观",40,310);
    button(mode_page,"返回首页",32,382,326,40,GY_UI_HOME);
    text_center(prep_page,"人机对战",32,28,326);
    text_center(prep_page,"选择先后手",32,58,326);
    text_center(prep_page,"先手",32,92,150);
    text_center(prep_page,"后手",208,92,150);
    side_box(prep_page,32,118,150,170,GY_UI_PREP_SWAP,0xff6b7c,
             &prep_red_label,&prep_player_icon[0],&prep_ai_icon[0]);
    side_box(prep_page,208,118,150,170,GY_UI_PREP_SWAP,0x5ba8ff,
             &prep_blue_label,&prep_player_icon[1],&prep_ai_icon[1]);
    text_center(prep_page,"点击方框交换先后手",32,300,326);
    button(prep_page,"取消",32,350,150,44,GY_UI_PREP_CANCEL);
    button(prep_page,"开始游戏",208,350,150,44,GY_UI_PREP_START);
    update_prep_labels();
    text(param_page,"参数",32,12);
    text(param_page,"新开一局生效",32,38);
    param_mode_buttons[0]=button(param_page,"单机",32,64,104,34,GY_UI_PARAM_MODE_SINGLE);
    param_mode_buttons[1]=button(param_page,"人机",143,64,104,34,GY_UI_PARAM_MODE_PVE);
    param_mode_buttons[2]=button(param_page,"联机",254,64,104,34,GY_UI_PARAM_MODE_PVP);
    param_prev_button=button(param_page,"上一页",16,304,100,28,GY_UI_PARAM_PREV);
    param_next_button=button(param_page,"下一页",274,304,100,28,GY_UI_PARAM_NEXT);
    param_page_label=text(param_page,"1/2",170,304);
    button(param_page,"重置",32,340,150,38,GY_UI_PARAM_RESET);
    button(param_page,"返回",208,340,150,38,GY_UI_PARAM_BACK);
    param_status_label=text(param_page,"新开一局生效",16,388);
    lv_obj_set_width(param_status_label,358);
    for(row=0;row<GY_PARAM_UI_ROWS;++row){
        lv_obj_t *r=lv_obj_create(param_page);
        lv_obj_set_pos(r,16,106+row*38);lv_obj_set_size(r,358,34);
        lv_obj_set_style_bg_opa(r,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(r,0,0);
        lv_obj_set_style_pad_all(r,0,0);
        lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);
        param_row_obj[row]=r;
        param_row_name[row]=text(r,"",0,7);lv_obj_set_width(param_row_name[row],120);
        param_row_value[row]=text(r,"",126,7);lv_obj_set_width(param_row_value[row],120);
        {lv_obj_t *b=lv_button_create(r);lv_obj_set_pos(b,250,1);lv_obj_set_size(b,44,32);
         lv_obj_set_style_radius(b,7,0);lv_obj_set_style_shadow_width(b,0,0);
         lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,"-");lv_obj_center(l);
         lv_obj_add_event_cb(b,param_adjust_event,LV_EVENT_CLICKED,(void*)(intptr_t)(row<<1));}
        {lv_obj_t *b=lv_button_create(r);lv_obj_set_pos(b,300,1);lv_obj_set_size(b,44,32);
         lv_obj_set_style_radius(b,7,0);lv_obj_set_style_shadow_width(b,0,0);
         lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,"+");lv_obj_center(l);
         lv_obj_add_event_cb(b,param_adjust_event,LV_EVENT_CLICKED,(void*)(intptr_t)((row<<1)|1));}
    }
    {int i;
     text(net_lobby_page,"联机大厅",32,26);
     net_lobby_status=text(net_lobby_page,"扫描中...",32,60);lv_obj_set_width(net_lobby_status,326);
     for(i=0;i<5;++i)
         net_room_rows[i]=button(net_lobby_page,"空房间位",32,96+i*46,326,40,GY_UI_NET_JOIN0+i);
     button(net_lobby_page,"创建房间",32,340,150,40,GY_UI_NET_CREATE);
     button(net_lobby_page,"返回",208,340,150,40,GY_UI_NET_BACK);
     text(net_room_page,"联机房间",32,20);
     net_room_status=text(net_room_page,"连接中...",32,52);lv_obj_set_width(net_room_status,326);
     text_center(net_room_page,"先手",16,80,170);
     text_center(net_room_page,"后手",204,80,170);
     net_red_box=net_seat_box(net_room_page,16,106,170,170,GY_UI_NET_SWAP,
                              &net_red_name,&net_red_state);
     net_blue_box=net_seat_box(net_room_page,204,106,170,170,GY_UI_NET_SWAP,
                               &net_blue_name,&net_blue_state);
     net_spectator_label=text(net_room_page,"观战 0 人",16,282);
     net_room_hint=text_center(net_room_page,"房主可在准备前交换先后手",16,304,358);
     net_leave_button=button(net_room_page,"退出房间",16,350,170,44,GY_UI_NET_LEAVE);
     net_ready_button=button(net_room_page,"准备",204,350,170,44,GY_UI_NET_READY);
    }
    root=game_page;
    /* 真机为圆角屏：左上角不放 GEYI；右上角“认输/返回首页/退出旁观”按钮左移，
     * 避开板边和右手遮挡，同时仍在圆角安全区内。 */
    resign_button=button(root,"认输",240,22,96,36,GY_UI_RESIGN);
    /* 左上圆角 R=48：回合标签原在 (16,26) 会被圆角裁掉；右移到
     * 棋盘左边界附近，既避开圆角也不压右上认输按钮。 */
    turn_label=text(root,"RED TURN",90,24);money_label=text(root,"",16,50);
    for(y=0;y<GY_MAX;++y)for(x=0;x<GY_MAX;++x) {
        lv_obj_t *tile=lv_obj_create(root);tiles[y][x]=tile;
        lv_obj_set_pos(tile,0,0);lv_obj_set_size(tile,1,1);
        lv_obj_set_style_radius(tile,5,0);lv_obj_set_style_pad_all(tile,0,0);
        lv_obj_remove_flag(tile,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tile,cell_event,LV_EVENT_CLICKED,(void*)(intptr_t)(y*GY_MAX+x));
        labels[y][x]=lv_label_create(tile);lv_obj_set_style_text_font(labels[y][x],&lv_font_montserrat_18,0);
        health[y][x]=lv_bar_create(tile);lv_obj_set_size(health[y][x],27,4);lv_obj_align(health[y][x],LV_ALIGN_BOTTOM_MID,0,-3);
        lv_bar_set_range(health[y][x],0,GY_HP_MAX);lv_obj_set_style_bg_color(health[y][x],lv_color_hex(0xffffff),LV_PART_INDICATOR);
        lv_obj_remove_flag(health[y][x],LV_OBJ_FLAG_CLICKABLE);
    }
    button(root,LV_SYMBOL_UP,16,330,40,30,GY_UI_UP);button(root,LV_SYMBOL_RIGHT,62,330,40,30,GY_UI_RIGHT);
    button(root,LV_SYMBOL_DOWN,108,330,40,30,GY_UI_DOWN);button(root,LV_SYMBOL_LEFT,154,330,40,30,GY_UI_LEFT);
    button(root,"-",210,330,32,30,GY_UI_LESS);count_label=text(root,"",248,339);
    button(root,"+",344,330,32,30,GY_UI_MORE);
    confirm_button=button(root,"确认",16,368,115,32,GY_UI_CONFIRM);
    lv_obj_set_style_opa(confirm_button,LV_OPA_40,LV_STATE_DISABLED);
    button(root,"取消",139,368,110,32,GY_UI_CANCEL);
    place_button=button(root,"放置",257,368,118,32,GY_UI_PLACE);
    hint=text(root,"",24,404);lv_obj_set_size(hint,342,30);
    lv_obj_set_style_bg_color(hint,lv_color_hex(0x102033),0);
    lv_obj_set_style_bg_opa(hint,LV_OPA_COVER,0);
    lv_obj_set_style_radius(hint,6,0);
    lv_obj_set_style_pad_all(hint,5,0);
    lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);

    /* 对局中断线重连：黄底大字横幅,和房间页的 10s 倒计时一致。 */
    net_reconn_overlay=lv_obj_create(root);
    lv_obj_set_size(net_reconn_overlay,340,58);
    lv_obj_align(net_reconn_overlay,LV_ALIGN_TOP_MID,0,86);
    lv_obj_set_style_bg_color(net_reconn_overlay,lv_color_hex(0xffdc79),0);
    lv_obj_set_style_bg_opa(net_reconn_overlay,LV_OPA_COVER,0);
    lv_obj_set_style_radius(net_reconn_overlay,10,0);
    lv_obj_set_style_border_width(net_reconn_overlay,2,0);
    lv_obj_set_style_border_color(net_reconn_overlay,lv_color_hex(0x8a5a00),0);
    lv_obj_set_style_pad_all(net_reconn_overlay,0,0);
    lv_obj_remove_flag(net_reconn_overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(net_reconn_overlay,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(net_reconn_overlay,LV_OBJ_FLAG_HIDDEN);
    net_reconn_label=lv_label_create(net_reconn_overlay);
    lv_obj_set_width(net_reconn_label,320);
    lv_obj_align(net_reconn_label,LV_ALIGN_CENTER,0,0);
    lv_obj_set_style_text_font(net_reconn_label,&geyi_font_cn16,0);
    lv_obj_set_style_text_color(net_reconn_label,lv_color_hex(0x142238),0);
    lv_obj_set_style_text_align(net_reconn_label,LV_TEXT_ALIGN_CENTER,0);

    /* 结算弹窗：比左下角小字更醒目,4s 后房间自动回到 WAIT。 */
    net_result_overlay=lv_obj_create(root);
    lv_obj_set_size(net_result_overlay,340,210);
    lv_obj_align(net_result_overlay,LV_ALIGN_CENTER,0,0);
    lv_obj_set_style_bg_color(net_result_overlay,lv_color_hex(0x101a2b),0);
    lv_obj_set_style_bg_opa(net_result_overlay,LV_OPA_COVER,0);
    lv_obj_set_style_radius(net_result_overlay,16,0);
    lv_obj_set_style_border_width(net_result_overlay,3,0);
    lv_obj_set_style_border_color(net_result_overlay,lv_color_hex(0xffdc79),0);
    lv_obj_set_style_shadow_width(net_result_overlay,30,0);
    lv_obj_set_style_pad_all(net_result_overlay,0,0);
    lv_obj_remove_flag(net_result_overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(net_result_overlay,LV_OBJ_FLAG_CLICKABLE);
    net_result_title=lv_label_create(net_result_overlay);
    lv_obj_set_width(net_result_title,300);
    lv_obj_align(net_result_title,LV_ALIGN_TOP_MID,0,42);
    lv_obj_set_style_text_font(net_result_title,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_color(net_result_title,lv_color_hex(0xffdc79),0);
    lv_obj_set_style_text_align(net_result_title,LV_TEXT_ALIGN_CENTER,0);
    net_result_sub=lv_label_create(net_result_overlay);
    lv_obj_set_width(net_result_sub,300);
    lv_obj_align(net_result_sub,LV_ALIGN_TOP_MID,0,100);
    lv_obj_set_style_text_color(net_result_sub,lv_color_hex(0xe7edf8),0);
    lv_obj_set_style_text_align(net_result_sub,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_add_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);

    /* 全屏半透明遮挡：确认弹窗打开时禁止继续操作棋盘/按钮。 */
    net_confirm_block=lv_obj_create(root);
    lv_obj_set_size(net_confirm_block,390,450);
    lv_obj_set_pos(net_confirm_block,0,0);
    lv_obj_set_style_bg_color(net_confirm_block,lv_color_hex(0x000000),0);
    lv_obj_set_style_bg_opa(net_confirm_block,LV_OPA_50,0);
    lv_obj_set_style_border_width(net_confirm_block,0,0);
    lv_obj_set_style_radius(net_confirm_block,0,0);
    lv_obj_set_style_pad_all(net_confirm_block,0,0);
    lv_obj_remove_flag(net_confirm_block,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(net_confirm_block,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(net_confirm_block,LV_OBJ_FLAG_HIDDEN);

    /* 退出/认输确认弹窗：一次点击打开,一次确认,不再靠连按。 */
    net_confirm_overlay=lv_obj_create(root);
    lv_obj_set_size(net_confirm_overlay,330,190);
    lv_obj_align(net_confirm_overlay,LV_ALIGN_CENTER,0,0);
    lv_obj_set_style_bg_color(net_confirm_overlay,lv_color_hex(0x101a2b),0);
    lv_obj_set_style_bg_opa(net_confirm_overlay,LV_OPA_COVER,0);
    lv_obj_set_style_radius(net_confirm_overlay,14,0);
    lv_obj_set_style_border_width(net_confirm_overlay,2,0);
    lv_obj_set_style_border_color(net_confirm_overlay,lv_color_hex(0xff6b7c),0);
    lv_obj_set_style_shadow_width(net_confirm_overlay,24,0);
    lv_obj_set_style_pad_all(net_confirm_overlay,0,0);
    lv_obj_remove_flag(net_confirm_overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(net_confirm_overlay,LV_OBJ_FLAG_CLICKABLE);
    net_confirm_label=lv_label_create(net_confirm_overlay);
    lv_obj_set_width(net_confirm_label,290);
    lv_obj_align(net_confirm_label,LV_ALIGN_TOP_MID,0,28);
    lv_obj_set_style_text_color(net_confirm_label,lv_color_hex(0xe7edf8),0);
    lv_obj_set_style_text_align(net_confirm_label,LV_TEXT_ALIGN_CENTER,0);
    button(net_confirm_overlay,"取消",30,124,120,40,GY_UI_NET_CONFIRM_NO);
    button(net_confirm_overlay,"确认",180,124,120,40,GY_UI_NET_CONFIRM_YES);
    lv_obj_add_flag(net_confirm_overlay,LV_OBJ_FLAG_HIDDEN);

    gy_param_ui_init();
    param_refresh();
    gy_ui_action(GY_UI_SINGLE);gy_ui_action(GY_UI_HOME);
}
static int corner_point_ok(int x,int y,int w,int h,int r)
{
    if(x<0||y<0||x>=w||y>=h)return 0;
    if(x<r&&y<r&&(r-x)*(r-x)+(r-y)*(r-y)>r*r)return 0;
    if(x>=w-r&&y<r&&(x-(w-1-r))*(x-(w-1-r))+(r-y)*(r-y)>r*r)return 0;
    if(x<r&&y>=h-r&&(r-x)*(r-x)+(y-(h-1-r))*(y-(h-1-r))>r*r)return 0;
    if(x>=w-r&&y>=h-r&&
       (x-(w-1-r))*(x-(w-1-r))+(y-(h-1-r))*(y-(h-1-r))>r*r)return 0;
    return 1;
}
static int corner_walk(lv_obj_t *obj,int r,int w,int h)
{
    int bad=0,i;
    if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return 0;
    if(lv_obj_check_type(obj,&lv_button_class)||lv_obj_check_type(obj,&lv_label_class)){
        lv_area_t a;lv_obj_get_coords(obj,&a);
        if(!corner_point_ok(a.x1,a.y1,w,h,r)||!corner_point_ok(a.x2,a.y1,w,h,r)||
           !corner_point_ok(a.x1,a.y2,w,h,r)||!corner_point_ok(a.x2,a.y2,w,h,r))bad++;
    }
    for(i=0;i<(int)lv_obj_get_child_count(obj);++i)
        bad+=corner_walk(lv_obj_get_child(obj,i),r,w,h);
    return bad;
}
int gy_ui_corner_clip_count(int radius)
{
    int w=LV_HOR_RES,h=LV_VER_RES;
    if(radius<=0||w<=0||h<=0)return -1;
    if(radius*2>w||radius*2>h)return -2;
    return corner_walk(lv_screen_active(),radius,w,h);
}
int gy_ui_rules_active(void) {return rules_active;}
int gy_ui_rules_page(void) {return rules_index;}
int gy_ui_rules_content_bottom(void)
{
    lv_area_t a;
    if(!rules_content)return 0;
    lv_obj_get_coords(rules_content,&a);
    return a.y2;
}
void gy_ui_key2(void)
{
    if(!playing)return;
    if(gy_ui_is_placing()){
        /* 放兵模式：选好底线列就确认；还没选列则退出放兵。 */
        gy_ui_action(gy_ui_has_preview()?GY_UI_CONFIRM:GY_UI_CANCEL);
    }else if(gy_ui_has_preview()){
        /* 倾斜预览：KEY2 确认落子；取消靠板子回中，不用按键。 */
        gy_ui_action(GY_UI_CONFIRM);
    }else{
        /* 未倾斜：进入/退出放兵模式。 */
        gy_ui_action(GY_UI_PLACE);
    }
}
const GyGame *gy_ui_game(void) {return &game;}
int gy_ui_has_preview(void) {return ready;}
int gy_ui_is_placing(void) {return placing;}
int gy_ui_is_playing(void) {return playing;}
int gy_ui_ai_mode(void) {return ai_side>=0;}
int gy_ui_ai_thinking(void) {return ai_thinking;}
int gy_ui_ai_previewing(void) {return ai_preview_pending;}
int gy_ui_prep_active(void) {return prep_active;}
int gy_ui_prep_human_red(void) {return prep_human_red;}
int gy_ui_param_active(void) {return param_active;}
int gy_ui_take_feedback(void) {int event=feedback;feedback=GY_FX_NONE;return event;}

#ifdef GEYI_WITH_RTTHREAD
#define GY_TEST_LOG rt_kprintf
#else
#define GY_TEST_LOG printf
#endif

static char test_command[64];
void gy_ui_test_request(const char *command)
{
    if(!command)return;
    strncpy(test_command,command,sizeof(test_command)-1);
    test_command[sizeof(test_command)-1]=0;
}
static void run_test_command(void)
{
    char cmd[64];
    int a=0,b=0;
    if(!test_command[0])return;
    strncpy(cmd,test_command,sizeof(cmd)-1);cmd[sizeof(cmd)-1]=0;
    test_command[0]=0;
#ifdef GEYI_WITH_RTTHREAD
    /* 自动化输入注入：仅替换 IMU/KEY2 采样源，UI 与状态机路径不变。 */
    if(sscanf(cmd,"simtilt %d %d",&a,&b)==2){
        gy_board_test_tilt(a,b);
        GY_TEST_LOG("[gy_test] simtilt x=%d y=%d\n",a,b);
        return;
    }
    if(!strcmp(cmd,"simkey")){
        gy_board_test_key();
        GY_TEST_LOG("[gy_test] simkey\n");
        return;
    }
    if(!strcmp(cmd,"simoff")){
        gy_board_test_off();
        GY_TEST_LOG("[gy_test] simoff\n");
        return;
    }
    if(sscanf(cmd,"simcoin %d",&a)==1){
        int ok=gy_net_app_test_set_coins(a);
        GY_TEST_LOG("[gy_test] simcoin value=%d ok=%d\n",a,ok);
        return;
    }
#endif
    if(!strcmp(cmd,"rules")){gy_ui_action(GY_UI_RULES);return;}
    if(!strcmp(cmd,"rulesnext")){gy_ui_action(GY_UI_RULES_NEXT);return;}
    if(!strcmp(cmd,"rulesprev")){gy_ui_action(GY_UI_RULES_PREV);return;}
    if(!strcmp(cmd,"rulesback")){gy_ui_action(GY_UI_RULES_BACK);return;}
    if(!strcmp(cmd,"home")){gy_ui_action(GY_UI_HOME);return;}
    if(!strcmp(cmd,"host")){gy_ui_action(GY_UI_NET_CREATE);return;}
    if(!strcmp(cmd,"lobby")){gy_ui_action(GY_UI_NET);return;}
    if(!strcmp(cmd,"ready")){gy_ui_action(GY_UI_NET_READY);return;}
    if(!strcmp(cmd,"swap")){gy_ui_action(GY_UI_NET_SWAP);return;}
    if(!strcmp(cmd,"leave")){gy_ui_action(GY_UI_NET_LEAVE);return;}
    if(!strcmp(cmd,"resign")){gy_net_app_resign();return;}
    if(!strcmp(cmd,"drop")){gy_net_app_test_drop();return;}
    if(sscanf(cmd,"join %d",&a)==1){gy_ui_action(GY_UI_NET_JOIN0+a);return;}
    if(sscanf(cmd,"watch %d",&a)==1){
        if(gy_net_app_join_index_role(a,GY_NET_ROLE_SPECTATOR)==0){
            show_page(net_room_page);net_room_refresh();
        }else net_lobby_refresh();
        return;
    }
    if(sscanf(cmd,"preview %d %d",&a,&b)==2){
        GyMove m;m.direction=a;m.count=b?b:1;m.aim=0;
        if(!gy_net_app_active()||!net_game_active||!gy_net_app_local_turn()){
            GY_TEST_LOG("[gy_test] preview rejected active=%d turn=%d\n",
                       gy_net_app_active(),gy_net_app_local_turn());return;
        }
        direction=a;amount=m.count;selected_count=amount;
        ready=gy_preview(&game,m,&preview);
        if(!ready){GY_TEST_LOG("[gy_test] preview invalid dir=%d count=%d\n",a,b);return;}
        gy_net_app_preview(a,selected_count,0);
        GY_TEST_LOG("[gy_test] preview sent dir=%d count=%d\n",a,selected_count);
        return;
    }
    if(!strcmp(cmd,"cancel")){
        gy_net_app_cancel();reset_selection_ui();gy_nav_clear(&nav);
        GY_TEST_LOG("[gy_test] cancel sent\n");
        return;
    }
    if(sscanf(cmd,"move %d %d",&a,&b)==2){
        GyMove m;m.direction=a;m.count=b;m.aim=0;
        if(!gy_net_app_active()||!net_game_active||!gy_net_app_local_turn()){
            GY_TEST_LOG("[gy_test] move rejected active=%d turn=%d\n",
                       gy_net_app_active(),gy_net_app_local_turn());return;
        }
        direction=a;amount=b?b:1;selected_count=amount;
        ready=gy_preview(&game,m,&preview);
        if(!ready){GY_TEST_LOG("[gy_test] move preview=0 dir=%d count=%d\n",a,b);return;}
        gy_net_app_preview(a,selected_count,0);
        gy_ui_action(GY_UI_CONFIRM);
        GY_TEST_LOG("[gy_test] move sent dir=%d count=%d\n",a,b);
        return;
    }
    if(sscanf(cmd,"place %d",&a)==1){
        int row;
        if(!gy_net_app_active()||!net_game_active||!gy_net_app_local_turn()){
            GY_TEST_LOG("[gy_test] place rejected active=%d turn=%d\n",
                       gy_net_app_active(),gy_net_app_local_turn());return;
        }
        row=game.turn?0:game.config.size-1;
        if(a<0||a>=game.config.size||!gy_place_legal(&game,row,a)){
            GY_TEST_LOG("[gy_test] place invalid row=%d col=%d\n",row,a);return;
        }
        /* 走与触摸/倾斜选格后相同的 UI 确认路径：必须先把 UI 的
         * ready/placing 置好，否则 gy_ui_action(CONFIRM) 会在
         * “没有可确认的选择”处提前返回，命令看似成功但不会发包。 */
        placing=1;place_row=row;place_column=a;ready=1;
        if(!gy_net_app_place(row,a)){
            placing=0;ready=0;
            GY_TEST_LOG("[gy_test] place rejected net row=%d col=%d\n",row,a);
            return;
        }
        gy_ui_action(GY_UI_CONFIRM);
        GY_TEST_LOG("[gy_test] place sent row=%d col=%d accepted=%d\n",
                   row,a,!ready);
        return;
    }
    if(!strcmp(cmd,"suggest")){
        const GyGame *ng=gy_net_app_game();
        int d;
        if(!gy_net_app_active()||!gy_net_app_local_turn()){
            GY_TEST_LOG("[gy_test] suggest none active=%d turn=%d\n",
                       gy_net_app_active(),gy_net_app_local_turn());
            GY_TEST_LOG("[gy_test] end\n");
            return;
        }
        for(d=0;d<4;++d)if(gy_eligible_count(ng,d)>0){
            GY_TEST_LOG("[gy_test] suggest move %d 1\n",d);
            GY_TEST_LOG("[gy_test] end\n");
            return;
        }
        for(d=0;d<ng->config.size;++d){
            GyGame copy=*ng;
            if(gy_place(&copy,d)){
                GY_TEST_LOG("[gy_test] suggest place %d\n",d);
                GY_TEST_LOG("[gy_test] end\n");
                return;
            }
        }
        GY_TEST_LOG("[gy_test] suggest none\n");
        GY_TEST_LOG("[gy_test] end\n");
        return;
    }
    if(!strcmp(cmd,"view")){
        const char *rs=net_red_state?lv_label_get_text(net_red_state):"";
        const char *bs=net_blue_state?lv_label_get_text(net_blue_state):"";
        GY_TEST_LOG("[gy_test] view red=%s|%s\n",
                   net_red_name?lv_label_get_text(net_red_name):"",rs?rs:"");
        GY_TEST_LOG("[gy_test] view blue=%s|%s\n",
                   net_blue_name?lv_label_get_text(net_blue_name):"",bs?bs:"");
        GY_TEST_LOG("[gy_test] view status=%s\n",
                   net_room_status?lv_label_get_text(net_room_status):"");
        GY_TEST_LOG("[gy_test] view hint=%s\n",
                   net_room_hint?lv_label_get_text(net_room_hint):"");
        GY_TEST_LOG("[gy_test] view readybtn=%d\n",
                   net_ready_button&&!lv_obj_has_flag(net_ready_button,LV_OBJ_FLAG_HIDDEN));
        GY_TEST_LOG("[gy_test] view reconn=%d\n",
                   net_reconn_overlay&&!lv_obj_has_flag(net_reconn_overlay,LV_OBJ_FLAG_HIDDEN));
        GY_TEST_LOG("[gy_test] view reconn_text=%s\n",
                   net_reconn_label?lv_label_get_text(net_reconn_label):"");
        GY_TEST_LOG("[gy_test] view turn=%s\n",
                   turn_label?lv_label_get_text(turn_label):"");
        GY_TEST_LOG("[gy_test] view result=%s\n",
                   net_result_sub?lv_label_get_text(net_result_sub):"");
        GY_TEST_LOG("[gy_test] ui rules=%d page=%d corner=%d rbottom=%d playing=%d\n",
                   gy_ui_rules_active(),gy_ui_rules_page(),
                   gy_ui_corner_clip_count(48),gy_ui_rules_content_bottom(),
                   gy_ui_is_playing());
        GY_TEST_LOG("[gy_test] end\n");
        return;
    }
    if(!strcmp(cmd,"state")){
        const GyGame *ng=gy_net_app_game();
        /* 分三行输出：RT-Thread 控制台单行缓冲约 128 B,整行会被截断。 */
        GY_TEST_LOG("[gy_test] state active=%d role=%d phase=%d players=%d "
                   "spectators=%d seq=%u rooms=%d turn=%d rev=%u adv=%d\n",
                   gy_net_app_active(),gy_net_app_role(),gy_net_app_phase(),
                   gy_net_app_player_count(),gy_net_app_spectator_count(),
                   (unsigned)gy_net_app_seq(),gy_net_app_room_count(),
                   ng->turn,(unsigned)ng->revision,gy_net_app_host_adv_active());
        /* RT-Thread 控制台单行约 128 B，超过会被截断；拆成两行。 */
        GY_TEST_LOG("[gy_test] info local=%d host=%d guest=%d id=%04X "
                   "hostid=%04X guestid=%04X human=%d ready=%d dir=%d count=%d "
                   "placing=%d\n",
                   gy_net_app_local_color(),gy_net_app_host_color(),
                   gy_net_app_guest_color(),(unsigned)gy_net_app_local_id(),
                   (unsigned)gy_net_app_seat_id(GY_NET_COLOR_RED),
                   (unsigned)gy_net_app_seat_id(GY_NET_COLOR_BLUE),
                   human_turn(),ready,direction,ready?selected_count:0,placing);
        GY_TEST_LOG("[gy_test] info2 prow=%d pcol=%d pending=%d coinr=%d coinb=%d "
                   "cost=%d\n",
                   place_row,place_column,gy_net_app_pending_move(),
                   game.coins[GY_NET_COLOR_RED],game.coins[GY_NET_COLOR_BLUE],
                   game.config.cost);
        GY_TEST_LOG("[gy_test] ready hready=%d gready=%d rready=%d bready=%d "
                   "outcome=%d result=%d joinrej=%d joinreason=%d cd=%d\n",
                   gy_net_app_host_ready(),gy_net_app_guest_ready(),
                   gy_net_app_seat_ready(GY_NET_COLOR_RED),
                   gy_net_app_seat_ready(GY_NET_COLOR_BLUE),
                   ng->outcome,gy_net_app_result(),
                   gy_net_app_join_rejected(),gy_net_app_join_reason(),
                   gy_net_app_countdown_seconds());
        GY_TEST_LOG("[gy_test] recon peer=%d reconn=%d seatr=%d seatb=%d "
                   "secr=%d secb=%d\n",
                   gy_net_app_peer_online(),gy_net_app_reconnect_seconds(),
                   gy_net_app_seat_online(GY_NET_COLOR_RED),
                   gy_net_app_seat_online(GY_NET_COLOR_BLUE),
                   gy_net_app_seat_reconnect_seconds(GY_NET_COLOR_RED),
                   gy_net_app_seat_reconnect_seconds(GY_NET_COLOR_BLUE));
        {
            const GyNetPreview *pr=gy_net_app_preview_at(GY_NET_COLOR_RED);
            const GyNetPreview *pb=gy_net_app_preview_at(GY_NET_COLOR_BLUE);
            GY_TEST_LOG("[gy_test] spec spec=%d lost=%d off=%d rsec=%d "
                       "prevr=%d,%d,%d prevb=%d,%d,%d\n",
                       gy_net_app_is_spectator(),gy_net_app_spectator_lost(),
                       gy_net_app_offline_seat(),gy_net_app_remote_reconnect_seconds(),
                       gy_net_app_preview_visible(GY_NET_COLOR_RED),
                       pr?pr->direction:-1,pr?pr->count:0,
                       gy_net_app_preview_visible(GY_NET_COLOR_BLUE),
                       pb?pb->direction:-1,pb?pb->count:0);
        }
        GY_TEST_LOG("[gy_test] end\n");
        return;
    }
    GY_TEST_LOG("[gy_test] unknown command '%s'\n",cmd);
}
#ifdef GEYI_WITH_RTTHREAD
static int gy_net_cmd(int argc,char **argv)
{
    int i;char cmd[64];int n=0;
    cmd[0]=0;
    for(i=1;i<argc;i++){
        int len=(int)strlen(cmd);
        if(len&&len<(int)sizeof(cmd)-1)cmd[len++]=' ';
        strncpy(cmd+len,argv[i],sizeof(cmd)-1-len);
        cmd[sizeof(cmd)-1]=0;
        n++;
    }
    if(!n){GY_TEST_LOG("usage: gy_net host|lobby|join N|watch N|ready|swap|leave|preview D C|cancel|move D C|place C|simtilt X Y|simkey|simoff|simcoin N|state|view|rules|rulesnext|rulesprev|rulesback|home\n");return 0;}
    gy_ui_test_request(cmd);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(gy_net_cmd, gy_net, Geyi online board test command);
#endif

void gy_ui_tick(void)
{
    GyAiResult r;
    uint32_t now=lv_tick_get();
#ifdef GEYI_WITH_RTTHREAD
    /* AI 思考/预览期间不占 UI 线程写 Flash；历史 dirty 会在回合结束后落盘。 */
    if(!ai_thinking&&!ai_preview_pending)gy_history_store_flush(now);
#endif
    history_record_if_finished();
    run_test_command();
    gy_net_app_tick(now);
    history_net_tick(now);
    if(net_lobby_active)net_lobby_refresh();
    if(net_room_active&&gy_net_app_local_terminal()){
        /* 客方在房间页等房主 10s 未果：同样停留 2s 后回大厅。 */
        if(!net_terminal_at)net_terminal_at=now?now:1;
        else if((uint32_t)(now-net_terminal_at)>=2000u){
            net_terminal_at=0;
            gy_net_app_leave();
            gy_net_app_lobby_start();
            show_page(net_lobby_page);
            return;
        }
        net_room_refresh();
        return;
    }else if(net_room_active){
        net_terminal_at=0;
    }
    if(net_room_active){
        if(gy_net_app_phase()==GY_NET_PHASE_PLAYING){
            show_page(game_page);net_last_rev=UINT32_MAX;net_last_phase=-1;
        }else net_room_refresh();
    }
    if(net_game_active&&gy_net_app_local_terminal()){
        /* 客方 10s 内没等到房主：先显示“对方断线”，停留 2s 后回大厅；
         * 成功重连会清 local_terminal，不会走这里。 */
        if(!net_terminal_at)net_terminal_at=now?now:1;
        else if((uint32_t)(now-net_terminal_at)>=2000u){
            net_terminal_at=0;
            gy_net_app_leave();
            gy_net_app_lobby_start();
            show_page(net_lobby_page);
            return;
        }
        return;
    }else if(net_game_active){
        net_terminal_at=0;
    }
    if(net_game_active&&!gy_net_app_active()){
        if(gy_net_app_join_rejected()||gy_net_app_spectator_lost()){
            show_page(net_room_page);net_room_refresh();
        }else{
            gy_net_app_lobby_start();show_page(net_lobby_page);
        }
        return;
    }
    if(net_game_active&&gy_net_app_active()){
        const GyGame *ng=gy_net_app_game();
        int phase=gy_net_app_phase();
        if(phase==GY_NET_PHASE_RESULT){
            int res=gy_net_app_result();
            if(!res&&ng)res=ng->outcome;
            net_result_show(res);
        }else net_result_hide();
        if(phase==GY_NET_PHASE_WAIT){
            /* 结算停留结束：回同一房间,重新准备即可开下一局。 */
            net_result_hide();
            show_page(net_room_page);
            net_room_refresh();
            return;
        }
        if(ng&&ng->config.size>=GY_SIZE_MIN&&
           (ng->revision!=net_last_rev||phase!=net_last_phase)){
            if(ng->revision!=net_last_rev||phase!=GY_NET_PHASE_PLAYING){
                reset_selection_ui();
                gy_nav_clear(&nav);
                if(phase!=GY_NET_PHASE_PLAYING)net_confirm_hide();
                if(ng->revision!=net_last_rev)gy_net_app_cancel();
            }
            game=*ng;net_last_rev=ng->revision;net_last_phase=phase;
            layout_board();render();
        }
        {
            /* 旁观者看远端断线时本机 reconnect_seconds() 恒 0；必须用
             * remote_reconnect_seconds() 才能每秒重绘底部状态条。 */
            int rsec=gy_net_app_remote_reconnect_seconds();
            if(rsec!=net_last_reconn){net_last_reconn=rsec;render();}
            net_reconn_refresh();
        }
        if(place_button){
            if(gy_net_app_is_spectator())lv_obj_add_state(place_button,LV_STATE_DISABLED);
            else lv_obj_remove_state(place_button,LV_STATE_DISABLED);
        }
        if(resign_button){
            /* 旁观者也要能“退出旁观”,不能禁用这个唯一出口。 */
            lv_obj_remove_state(resign_button,LV_STATE_DISABLED);
        }
        return;
    }
    if(ai_preview_pending){
        if((uint32_t)(lv_tick_get()-ai_preview_at)>=AI_PREVIEW_MS)commit_ai_pending();
        return;
    }
    if(gy_ai_task_poll(&r)){
        if(!ai_thinking||r.generation!=ai_generation||r.revision!=game.revision||
           !playing||game.outcome!=GY_PLAYING||game.turn!=ai_side){
            ai_thinking=0;
        }else if(r.action.kind==0){
            ai_thinking=0;
            status("AI 没有合法动作");
            render();
            return;
        }else{
            ai_thinking=0;
            begin_ai_preview(&r.action);
            return;
        }
    }
    if(!ai_thinking&&!ai_preview_pending&&playing&&ai_side>=0&&
       game.outcome==GY_PLAYING&&game.turn==ai_side)
        start_ai();
}
void gy_ui_tilt_select(int d,int count,int aim_value)
{
    GyMove m={d,count,aim_value};
    /* 按钮预览优先：倾斜不得把按钮选的方向/数量改掉（REQ-319）。 */
    if(!gy_nav_tilt_allowed(&nav))return;
    if(gy_net_app_active()&&net_game_active){
        if(!human_turn()||gy_net_app_is_spectator()||placing||game.outcome)return;
        direction=d;selected_count=count;amount=count?count:1;aim=aim_value;
        ready=gy_preview(&game,m,&preview);
        if(ready){gy_nav_tilt_selected(&nav);gy_net_app_preview(d,selected_count,aim_value);}
        else{gy_nav_clear(&nav);status("方向不可用 请重新选择");}
        render();
        return;
    }
    if(!playing||ai_thinking||!human_turn()||placing||game.outcome)return;
    direction=d;selected_count=count;amount=count?count:1;aim=aim_value;
    ready=gy_preview(&game,m,&preview);
    if(ready){gy_nav_tilt_selected(&nav);}
    else{gy_nav_clear(&nav);status("方向不可用 请重新选择");}
    render();
}
/* 板子回中/输入失效：只取消倾斜预览；按钮预览由屏幕取消，不被回中打断。 */
void gy_ui_tilt_cancel(void)
{
    if(!gy_nav_tilt_cancel(&nav))return;
    reset_selection_ui();
    if(gy_net_app_active()&&net_game_active)gy_net_app_cancel();
    status("已取消 棋盘状态不变");
    render();
}
/* 板端在喂倾斜事件前询问：按钮预览期间倾斜应被忽略。 */
int gy_ui_tilt_allowed(void) {return gy_nav_tilt_allowed(&nav);}
int gy_ui_preview_direction(void) {return ready?direction:-1;}
int gy_ui_preview_count(void) {return ready?selected_count:0;}
int gy_ui_preview_aim(void) {return ready?aim:0;}
uint32_t gy_ui_session(void) {return ui_session;}
#endif
