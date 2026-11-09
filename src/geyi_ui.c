#ifdef GEYI_WITH_LVGL
#include "geyi_ui.h"
#include "geyi_ai.h"
#include "geyi_ai_task.h"
#include "geyi_config.h"
#include "geyi_param_ui.h"
#include "geyi_net_app.h"
#include "geyi_font_cn16.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GEYI_WITH_RTTHREAD
#include "finsh.h"
#endif

static GyGame game;
static GyPreview preview;
static int direction=-1, amount=1, selected_count=1, placing=0, place_column=-1, ready=0;
static lv_obj_t *tiles[GY_MAX][GY_MAX], *labels[GY_MAX][GY_MAX], *health[GY_MAX][GY_MAX];
static lv_obj_t *turn_label,*money_label,*hint,*count_label,*confirm_button;
static lv_obj_t *home_page,*mode_page,*game_page,*prep_page,*param_page;
static lv_obj_t *net_lobby_page,*net_room_page;
static lv_obj_t *net_lobby_status,*net_room_status;
static lv_obj_t *net_room_rows[5];
static lv_obj_t *net_red_name,*net_red_state,*net_blue_name,*net_blue_state;
static lv_obj_t *net_spectator_label;
static lv_obj_t *net_red_box,*net_blue_box,*net_room_hint;
static lv_obj_t *net_ready_button,*net_leave_button,*place_button,*resign_button;
static lv_obj_t *net_result_overlay,*net_result_title,*net_result_sub;
static lv_obj_t *net_confirm_block,*net_confirm_overlay,*net_confirm_label;
static int net_lobby_active,net_room_active,net_game_active;
static uint32_t net_last_rev;static int net_last_phase=-1;
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
static void show_page(lv_obj_t *page)
{
    cancel_ai();
    lv_obj_add_flag(home_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mode_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(game_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(prep_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(param_page,LV_OBJ_FLAG_HIDDEN);
    if(net_lobby_page)lv_obj_add_flag(net_lobby_page,LV_OBJ_FLAG_HIDDEN);
    if(net_room_page)lv_obj_add_flag(net_room_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page,LV_OBJ_FLAG_HIDDEN);
    playing=page==game_page;
    prep_active=page==prep_page;
    param_active=page==param_page;
    net_lobby_active=page==net_lobby_page;
    net_room_active=page==net_room_page;
    net_game_active=page==game_page&&gy_net_app_active();
    if(page!=game_page){
        if(net_result_overlay)lv_obj_add_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);
        if(net_confirm_block)lv_obj_add_flag(net_confirm_block,LV_OBJ_FLAG_HIDDEN);
        if(net_confirm_overlay)lv_obj_add_flag(net_confirm_overlay,LV_OBJ_FLAG_HIDDEN);
    }
    ready=0;placing=0;direction=-1;place_column=-1;
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
    int local=gy_net_app_local_color();
    if(!net_room_status)return;
    {
        const char *fail=gy_net_app_join_reason()==GY_NET_JOIN_REASON_NOT_FOUND?"房间不存在":
                         gy_net_app_join_reason()==GY_NET_JOIN_REASON_FULL?"房间已满":
                         gy_net_app_join_reason()==GY_NET_JOIN_REASON_TIMEOUT?"加入超时":"加入失败";
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
        /* 准备中的座位统一变白，未准备保持红/蓝；只由该座位自己的 ready
         * 决定，绝不因为对方准备/自己不是房主而整块变白。 */
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
                lv_label_set_text(net_red_state,red_ready?"已准备":"未准备");
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
                lv_label_set_text(net_blue_state,blue_ready?"已准备":"未准备");
            }
            lv_obj_set_style_text_color(net_blue_name,
                lv_color_hex(blue_occ?0x142238:0xe7edf8),0);
            lv_obj_set_style_text_color(net_blue_state,
                lv_color_hex(blue_occ?0x142238:0xe7edf8),0);
        }
        (void)local;
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
    if(phase==GY_NET_PHASE_STARTING&&gy_net_app_active()){
        int sec=gy_net_app_countdown_seconds();
        if(net_spectator_label)lv_obj_add_flag(net_spectator_label,LV_OBJ_FLAG_HIDDEN);
        if(net_room_hint){
            /* 321 实时倒计时：黄底大字，比原来的小字明显。 */
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
         * 灰白色，导致“只有自己准备，双方框都变色”。点击是否有效由
         * gy_net_app_swap() 自己判断。 */
        if(net_room_hint){
            lv_label_set_text(net_room_hint,can?"点击方框交换先后手（准备前）":
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
static void net_result_show(int result)
{
    const char *title=(result==GY_RED_WIN)?"RED WIN":
                       (result==GY_BLUE_WIN)?"BLUE WIN":
                       (result==GY_DRAW)?"DRAW":"GAME OVER";
    const char *sub=(result==GY_RED_WIN)?"红方获胜，即将返回房间":
                     (result==GY_BLUE_WIN)?"蓝方获胜，即将返回房间":
                     (result==GY_DRAW)?"平局，即将返回房间":"即将返回房间";
    if(!net_result_overlay)return;
    if(net_result_shown==result &&
       !lv_obj_has_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN))return;
    net_result_shown=result;
    lv_label_set_text(net_result_title,title);
    lv_label_set_text(net_result_sub,sub);
    lv_obj_remove_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(net_result_overlay);
}
static void net_result_hide(void)
{
    net_result_shown=-1;
    if(net_result_overlay)lv_obj_add_flag(net_result_overlay,LV_OBJ_FLAG_HIDDEN);
}
static void net_confirm_show(void)
{
    if(!net_confirm_overlay)return;
    lv_label_set_text(net_confirm_label,"认输会让对手获胜，确定?");
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
    if(game.outcome)lv_label_set_text(turn_label,outcomes[game.outcome]);
    else if(gy_net_app_active()){
        if(gy_net_app_local_terminal())lv_label_set_text(turn_label,"对方断线");
        else{
            lv_snprintf(value,sizeof(value),"%s回合 %s",game.turn?"蓝方":"红方",
                gy_net_app_is_spectator()?"旁观":(gy_net_app_local_turn()?"(你)":"(对方)"));
            lv_label_set_text(turn_label,value);
        }
    }else if(ai_side>=0){
        lv_snprintf(value,sizeof(value),"%s回合%s",game.turn?"蓝方":"红方",game.turn==ai_side?"(AI)":"(你)");
        lv_label_set_text(turn_label,value);
    }else lv_label_set_text(turn_label,game.turn?"蓝方回合":"红方回合");
    lv_obj_set_style_text_color(turn_label,lv_color_hex(game.outcome==GY_DRAW?0xffdc79:
        color(game.outcome==GY_RED_WIN?0:game.outcome==GY_BLUE_WIN?1:game.turn)),0);
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
        if(placing&&y==(game.turn?0:size-1)) {
            lv_obj_set_style_border_width(tiles[y][x],2,0);
            lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(place_column==x?0xffdc79:0x65768f),0);
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
            nm.direction=np->direction;nm.count=np->count;
            if(!gy_preview(&game,nm,&nv))continue;
            for(y=0;y<size;++y)for(x=0;x<size;++x)
                if(memcmp(&nv.visible.cells[y][x],&game.cells[y][x],sizeof(GyCell))){
                    lv_obj_set_style_border_width(tiles[y][x],2,0);
                    lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(side?0x5ba8ff:0xff6b7c),0);
                }
        }
    }
    if(game.outcome)status(gy_net_app_active()?"本局结束，即将返回房间":
                                           "本局结束，点右上角返回首页");
    else if(gy_net_app_active()){
        if(gy_net_app_local_terminal())status("对方断线（本地提示，不是权威结算）");
        else if(gy_net_app_phase()==GY_NET_PHASE_STARTING)status("双方已准备 3 秒后开始");
        else if(gy_net_app_phase()!=GY_NET_PHASE_PLAYING)status(net_phase_text());
        else if(gy_net_app_is_spectator())status("旁观中 只读");
        else if(placing)status(place_column<0?"放置 请选择己方底线空位":"已选位置 请确认或取消");
        else if(ready){lv_snprintf(value,sizeof(value),"预览 %s %d 个 确认后发送",dirs[direction],selected_count);status(value);}
        else status(gy_net_app_local_turn()?"你的回合 先选方向":"对方回合 等待快照");
    }
    else if(ai_thinking)status("AI 思考中...");
    else if(ai_preview_pending)status("AI 预览 即将落子");
    else if(placing)status(place_column<0?"放置 请选择己方底线空位":"已选位置 请确认或取消");
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
    ready=0;placing=0;direction=-1;place_column=-1;
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
        ready=1;placing=0;place_column=-1;
    }else if(a->kind==GY_AI_PLACE){
        int row=game.turn==0?game.config.size-1:0;
        if(a->column<0||a->column>=game.config.size||
           game.cells[row][a->column].kind!=GY_EMPTY||
           game.coins[game.turn]<game.config.cost){apply_ai(a);return;}
        placing=1;ready=1;place_column=a->column;direction=-1;
    }else{apply_ai(a);return;}
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
    ready=0;placing=0;direction=-1;place_column=-1;
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
    m.direction=direction;m.count=available==0?0:(amount>available?available:amount);
    selected_count=m.count;
    ready=gy_preview(&game,m,&preview);
    if(!ready)status("方向不可用 请重新选择");
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
    m.direction=direction;m.count=available==0?0:(amount>available?available:amount);
    selected_count=m.count;
    ready=gy_preview(&game,m,&preview);
    if(!ready)status("方向不可用 请重新选择");
    else gy_net_app_preview(direction,selected_count);
}
void gy_ui_action(int a)
{
    uint32_t old_revision=game.revision;
    int old_outcome=game.outcome;
    if(a==GY_UI_NET_CONFIRM_NO){
        net_confirm_hide();return;
    }
    if(a==GY_UI_NET_CONFIRM_YES){
        net_confirm_hide();
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
    if(a==GY_UI_HOME){
        if(gy_net_app_active())gy_net_app_leave();
        gy_net_app_lobby_stop();
        ai_side=-1;feedback=GY_FX_CLEAR;show_page(home_page);return;
    }
    if(a==GY_UI_MODES){
        if(gy_net_app_active())gy_net_app_leave();
        gy_net_app_lobby_stop();
        ai_side=-1;feedback=GY_FX_CLEAR;show_page(mode_page);return;
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
        feedback=GY_FX_CLEAR;show_page(prep_page);return;
    }
    if(a==GY_UI_PREP_SWAP){
        if(!prep_active)return;
        prep_human_red=!prep_human_red;update_prep_labels();return;
    }
    if(a==GY_UI_PREP_CANCEL){ai_side=-1;feedback=GY_FX_CLEAR;show_page(mode_page);return;}
    if(a==GY_UI_PREP_START){
        ai_side=prep_human_red?1:0;game_mode=GY_MODE_PVE;show_page(game_page);a=GY_UI_RESTART;
    }
    if(a==GY_UI_RESIGN&&!gy_net_app_active()){
        if(game.outcome){ai_side=-1;feedback=GY_FX_CLEAR;show_page(home_page);}
        else if(playing){
            gy_resign(&game);ready=0;placing=0;direction=-1;
            if(game.outcome)feedback=game.outcome==GY_RED_WIN?GY_FX_RED:
                game.outcome==GY_BLUE_WIN?GY_FX_BLUE:GY_FX_DRAW;
            render();
        }
        return;
    }
    if(!playing)return;
    if(gy_net_app_active()&&net_game_active){
        if(a==GY_UI_RESIGN){
            if(gy_net_app_is_spectator()){
                gy_net_app_leave();gy_net_app_lobby_start();show_page(net_lobby_page);return;
            }
            if(gy_net_app_phase()==GY_NET_PHASE_PLAYING)net_confirm_show();
            else {gy_net_app_leave();gy_net_app_lobby_start();show_page(net_lobby_page);}
            return;
        }
        if(!human_turn()||gy_net_app_is_spectator()){render();return;}
        if(a==GY_UI_CANCEL){
            ready=0;placing=0;gy_net_app_cancel();status("已取消");render();return;
        }
        if(a<=GY_UI_LEFT){placing=0;direction=a;amount=1;choose_net();render();return;}
        if(a==GY_UI_LESS||a==GY_UI_MORE){
            amount+=a==GY_UI_MORE?1:-1;if(amount<1)amount=1;if(amount>36)amount=36;
            choose_net();render();return;
        }
        if(a==GY_UI_CONFIRM){
            int ok=ready&&gy_net_app_confirm();
            ready=0;placing=0;direction=-1;
            status(ok?"已发送 等待房主快照":"没有可确认的选择");render();return;
        }
        if(a==GY_UI_PLACE){
            ready=0;placing=0;place_column=-1;
            if(game.coins[game.turn]>=game.config.cost){
                placing=1;gy_net_app_cancel();gy_net_app_place(-1);
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
        direction=-1;amount=1;placing=0;ready=0;place_column=-1;
        status("先选方向 再确认");
    } else if(ai_thinking||!human_turn()) {
        render();return;
    } else if(a==GY_UI_CANCEL) {
        ready=0;placing=0;place_column=-1;direction=-1;status("已取消 棋盘状态不变");
    } else if(game.outcome) {render();return;}
    else if(a<=GY_UI_LEFT) {placing=0;direction=a;choose();}
    else if(a==GY_UI_LESS||a==GY_UI_MORE) {
        amount+=a==GY_UI_MORE?1:-1;if(amount<1)amount=1;if(amount>36)amount=36;choose();
    } else if(a==GY_UI_CONFIRM) {
        int ok=ready&&(placing?gy_place(&game,place_column):gy_commit(&game,&preview));
        ready=0;placing=0;direction=-1;status(ok?"回合结束 请交给对方":"没有可确认的选择");
    } else if(a==GY_UI_PLACE) {
        ready=0;placing=0;place_column=-1;
        if(game.coins[game.turn]>=game.config.cost)placing=1;
        else {char msg[48];lv_snprintf(msg,sizeof(msg),"需要%d金币才能放置",game.config.cost);status(msg);}
    }
    if(!old_outcome&&game.outcome)
        feedback=game.outcome==GY_RED_WIN?GY_FX_RED:game.outcome==GY_BLUE_WIN?GY_FX_BLUE:GY_FX_DRAW;
    else if(a==GY_UI_CONFIRM&&game.revision!=old_revision)feedback=GY_FX_MOVE;
    render();
    if(!game.outcome&&ai_side>=0&&game.turn==ai_side)start_ai();
}
void gy_ui_cell(int x,int y)
{
    int size=game.config.size;
    if(size<GY_SIZE_MIN||size>GY_SIZE_MAX)size=6;
    if(gy_net_app_active()&&net_game_active){
        if(!human_turn()||gy_net_app_is_spectator()||!placing||game.outcome||
           x<0||x>=size||y!=(game.turn?0:size-1))return;
        place_column=x;ready=game.cells[y][x].kind==GY_EMPTY;
        if(ready)gy_net_app_place(x);
        render();
        if(!ready)status("位置已被占用 请选择空位");
        return;
    }
    if(!playing||ai_thinking||!human_turn()||!placing||game.outcome||
       x<0||x>=size||y!=(game.turn?0:size-1))return;
    place_column=x;ready=game.cells[y][x].kind==GY_EMPTY;render();
    if(!ready)status("位置已被占用 请选择空位");
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
static void unavailable(lv_obj_t *root,const char *name,int y)
{
    lv_obj_t *b=button(root,name,32,y,326,48,GY_UI_HOME);
    lv_obj_add_state(b,LV_STATE_DISABLED);lv_obj_set_style_opa(b,LV_OPA_40,LV_STATE_DISABLED);
}
void gy_ui_create(void)
{
    int x,y,row;lv_obj_t *root=lv_screen_active();
    lv_obj_set_style_bg_color(root,lv_color_hex(0x111e31),0);
    lv_obj_set_style_text_font(root,&geyi_font_cn16,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    home_page=page(root);mode_page=page(root);game_page=page(root);prep_page=page(root);param_page=page(root);
    net_lobby_page=page(root);net_room_page=page(root);
    {lv_obj_t *t=text(home_page,"GEYI",32,46);lv_obj_set_style_text_font(t,&lv_font_montserrat_24,0);}
    text(home_page,"格弈 / 合成攻防",32,92);
    text(home_page,"倾斜棋盘  轮流对战",32,123);
    button(home_page,"开始游戏",32,188,326,52,GY_UI_MODES);
    button(home_page,"自定义游戏参数",32,258,326,48,GY_UI_PARAMS);
    unavailable(home_page,"历史记录 - 暂未开放",324);
    text(home_page,"单机 / 人机 / 联机",32,406);
    text(mode_page,"选择游戏模式",32,36);
    text(mode_page,"单机、人机或联机，开始一局对战",32,70);
    button(mode_page,"单机对战",32,104,326,44,GY_UI_SINGLE);
    text(mode_page,"两位玩家轮流操作一块开发板",40,152);
    button(mode_page,"人机对战",32,178,326,44,GY_UI_AI);
    text(mode_page,"进入后选择先后手，AI 自动走子",40,228);
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
    lv_obj_t *title=text(root,"GEYI",14,8);lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);
    resign_button=button(root,"认输",286,8,88,32,GY_UI_RESIGN);
    turn_label=text(root,"RED TURN",16,40);money_label=text(root,"",16,61);
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
    button(root,LV_SYMBOL_UP,16,339,40,30,GY_UI_UP);button(root,LV_SYMBOL_RIGHT,62,339,40,30,GY_UI_RIGHT);
    button(root,LV_SYMBOL_DOWN,108,339,40,30,GY_UI_DOWN);button(root,LV_SYMBOL_LEFT,154,339,40,30,GY_UI_LEFT);
    button(root,"-",210,339,32,30,GY_UI_LESS);count_label=text(root,"",248,348);
    button(root,"+",344,339,32,30,GY_UI_MORE);
    confirm_button=button(root,"确认",16,378,115,32,GY_UI_CONFIRM);
    lv_obj_set_style_opa(confirm_button,LV_OPA_40,LV_STATE_DISABLED);
    button(root,"取消",139,378,110,32,GY_UI_CANCEL);
    place_button=button(root,"放置",257,378,118,32,GY_UI_PLACE);
    hint=text(root,"",16,414);lv_obj_set_size(hint,358,30);
    lv_obj_set_style_bg_color(hint,lv_color_hex(0x102033),0);
    lv_obj_set_style_bg_opa(hint,LV_OPA_COVER,0);
    lv_obj_set_style_radius(hint,6,0);
    lv_obj_set_style_pad_all(hint,5,0);
    lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);

    /* 结算弹窗：比左下角小字更醒目，4s 后房间自动回到 WAIT。 */
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

    /* 退出/认输确认弹窗：一次点击打开，一次确认，不再靠连按。 */
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
    if(!strcmp(cmd,"host")){gy_ui_action(GY_UI_NET_CREATE);return;}
    if(!strcmp(cmd,"lobby")){gy_ui_action(GY_UI_NET);return;}
    if(!strcmp(cmd,"ready")){gy_ui_action(GY_UI_NET_READY);return;}
    if(!strcmp(cmd,"swap")){gy_ui_action(GY_UI_NET_SWAP);return;}
    if(!strcmp(cmd,"leave")){gy_ui_action(GY_UI_NET_LEAVE);return;}
    if(!strcmp(cmd,"resign")){gy_net_app_resign();return;}
    if(sscanf(cmd,"join %d",&a)==1){gy_ui_action(GY_UI_NET_JOIN0+a);return;}
    if(sscanf(cmd,"watch %d",&a)==1){
        if(gy_net_app_join_index_role(a,GY_NET_ROLE_SPECTATOR)==0){
            show_page(net_room_page);net_room_refresh();
        }else net_lobby_refresh();
        return;
    }
    if(sscanf(cmd,"move %d %d",&a,&b)==2){
        GyMove m;m.direction=a;m.count=b;
        if(!gy_net_app_active()||!net_game_active||!gy_net_app_local_turn()){
            GY_TEST_LOG("[gy_test] move rejected active=%d turn=%d\n",
                       gy_net_app_active(),gy_net_app_local_turn());return;
        }
        direction=a;amount=b?b:1;selected_count=amount;
        ready=gy_preview(&game,m,&preview);
        if(!ready){GY_TEST_LOG("[gy_test] move preview=0 dir=%d count=%d\n",a,b);return;}
        gy_net_app_preview(a,selected_count);
        gy_ui_action(GY_UI_CONFIRM);
        GY_TEST_LOG("[gy_test] move sent dir=%d count=%d\n",a,b);
        return;
    }
    if(sscanf(cmd,"place %d",&a)==1){
        if(!gy_net_app_active()||!net_game_active||!gy_net_app_local_turn()){
            GY_TEST_LOG("[gy_test] place rejected active=%d turn=%d\n",
                       gy_net_app_active(),gy_net_app_local_turn());return;
        }
        gy_net_app_place(a);
        gy_ui_action(GY_UI_CONFIRM);
        GY_TEST_LOG("[gy_test] place sent col=%d\n",a);
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
        GY_TEST_LOG("[gy_test] end\n");
        return;
    }
    if(!strcmp(cmd,"state")){
        const GyGame *ng=gy_net_app_game();
        /* 分三行输出：RT-Thread 控制台单行缓冲约 128 B，整行会被截断。 */
        GY_TEST_LOG("[gy_test] state active=%d role=%d phase=%d players=%d "
                   "spectators=%d seq=%u rooms=%d turn=%d rev=%u\n",
                   gy_net_app_active(),gy_net_app_role(),gy_net_app_phase(),
                   gy_net_app_player_count(),gy_net_app_spectator_count(),
                   (unsigned)gy_net_app_seq(),gy_net_app_room_count(),
                   ng->turn,(unsigned)ng->revision);
        GY_TEST_LOG("[gy_test] info local=%d host=%d guest=%d id=%04X "
                   "hostid=%04X guestid=%04X\n",
                   gy_net_app_local_color(),gy_net_app_host_color(),
                   gy_net_app_guest_color(),(unsigned)gy_net_app_local_id(),
                   (unsigned)gy_net_app_seat_id(GY_NET_COLOR_RED),
                   (unsigned)gy_net_app_seat_id(GY_NET_COLOR_BLUE));
        GY_TEST_LOG("[gy_test] ready hready=%d gready=%d rready=%d bready=%d "
                   "outcome=%d result=%d joinrej=%d joinreason=%d cd=%d\n",
                   gy_net_app_host_ready(),gy_net_app_guest_ready(),
                   gy_net_app_seat_ready(GY_NET_COLOR_RED),
                   gy_net_app_seat_ready(GY_NET_COLOR_BLUE),
                   ng->outcome,gy_net_app_result(),
                   gy_net_app_join_rejected(),gy_net_app_join_reason(),
                   gy_net_app_countdown_seconds());
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
    if(!n){GY_TEST_LOG("usage: gy_net host|lobby|join N|ready|swap|leave|move D C|place C|state\n");return 0;}
    gy_ui_test_request(cmd);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(gy_net_cmd, gy_net, Geyi online board test command);
#endif

void gy_ui_tick(void)
{
    GyAiResult r;
    uint32_t now=lv_tick_get();
    run_test_command();
    gy_net_app_tick(now);
    if(net_lobby_active)net_lobby_refresh();
    if(net_room_active){
        if(gy_net_app_phase()==GY_NET_PHASE_PLAYING){
            show_page(game_page);net_last_rev=UINT32_MAX;net_last_phase=-1;
        }else net_room_refresh();
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
            /* 结算停留结束：回同一房间，重新准备即可开下一局。 */
            net_result_hide();
            show_page(net_room_page);
            net_room_refresh();
            return;
        }
        if(ng&&ng->config.size>=GY_SIZE_MIN&&
           (ng->revision!=net_last_rev||phase!=net_last_phase)){
            if(ng->revision!=net_last_rev){ready=0;placing=0;direction=-1;place_column=-1;}
            game=*ng;net_last_rev=ng->revision;net_last_phase=phase;
            layout_board();render();
        }
        if(place_button){
            if(gy_net_app_is_spectator())lv_obj_add_state(place_button,LV_STATE_DISABLED);
            else lv_obj_remove_state(place_button,LV_STATE_DISABLED);
        }
        if(resign_button){
            /* 旁观者也要能“退出旁观”，不能禁用这个唯一出口。 */
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
void gy_ui_tilt_select(int d,int count)
{
    GyMove m={d,count};
    if(gy_net_app_active()&&net_game_active){
        if(!human_turn()||gy_net_app_is_spectator()||placing||game.outcome)return;
        direction=d;selected_count=count;amount=count?count:1;
        ready=gy_preview(&game,m,&preview);
        if(ready)gy_net_app_preview(d,selected_count);
        else status("方向不可用 请重新选择");
        render();
        return;
    }
    if(!playing||ai_thinking||!human_turn()||placing||game.outcome)return;
    direction=d;selected_count=count;amount=count?count:1;
    ready=gy_preview(&game,m,&preview);
    if(!ready)status("方向不可用 请重新选择");
    render();
}
#endif
