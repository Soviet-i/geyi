#ifdef GEYI_WITH_LVGL
#include "geyi_ui.h"
#include "geyi_ai.h"
#include "geyi_ai_task.h"
#include "geyi_font_cn16.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static GyGame game;
static GyPreview preview;
static int direction=-1, amount=1, selected_count=1, placing=0, place_column=-1, ready=0;
static lv_obj_t *tiles[6][6], *labels[6][6], *health[6][6];
static lv_obj_t *turn_label,*money_label,*hint,*count_label,*confirm_button;
static lv_obj_t *home_page,*mode_page,*game_page,*prep_page;
static lv_obj_t *prep_red_label,*prep_blue_label;
static int prep_active;
static int prep_human_red=1;
static int playing;
static int feedback;
static int ai_side=-1;          /* -1 = no AI, 0 = AI red, 1 = AI blue */
static int ai_thinking;
static int ai_preview_pending;
static uint32_t ai_preview_at;
static GyAiAction ai_pending;
static uint32_t ai_generation;
static uint32_t ai_revision;
#define AI_PREVIEW_MS 500u
static void cancel_ai(void)
{
    if(ai_thinking)gy_ai_task_cancel();
    ai_thinking=0;
    ai_preview_pending=0;
    ai_pending.kind=0;
    ++ai_generation;
}
static int human_turn(void){return ai_side<0||game.turn!=ai_side;}
static void show_page(lv_obj_t *page)
{
    cancel_ai();
    lv_obj_add_flag(home_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mode_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(game_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(prep_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page,LV_OBJ_FLAG_HIDDEN);
    playing=page==game_page;
    prep_active=page==prep_page;
    ready=0;placing=0;direction=-1;place_column=-1;
}
static void update_prep_labels(void)
{
    if(!prep_red_label||!prep_blue_label)return;
    lv_label_set_text(prep_red_label,prep_human_red?"红方\n先手\n玩家":"红方\n先手\nAI");
    lv_label_set_text(prep_blue_label,prep_human_red?"蓝方\n后手\nAI":"蓝方\n后手\n玩家");
}
static uint32_t color(int owner) {return owner?0x5ba8ff:0xff6b7c;}
static lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y)
{
    lv_obj_t *a=lv_label_create(parent);lv_label_set_text(a,value);lv_obj_set_pos(a,x,y);
    lv_obj_set_style_text_color(a,lv_color_hex(0xe7edf8),0);return a;
}
static void status(const char *value) {lv_label_set_text(hint,value);}
static void render(void)
{
    const GyGame *shown=ready&&!placing?&preview.visible:&game;
    int x,y;char value[80];
    const char *outcomes[]={"","红方胜","蓝方胜","平局"};
    const char *dirs[]={"上","右","下","左"};
    if(game.outcome)lv_label_set_text(turn_label,outcomes[game.outcome]);
    else if(ai_side>=0){
        lv_snprintf(value,sizeof(value),"%s回合%s",game.turn?"蓝方":"红方",game.turn==ai_side?"(AI)":"(你)");
        lv_label_set_text(turn_label,value);
    }else lv_label_set_text(turn_label,game.turn?"蓝方回合":"红方回合");
    lv_obj_set_style_text_color(turn_label,lv_color_hex(game.outcome==GY_DRAW?0xffdc79:
        color(game.outcome==GY_RED_WIN?0:game.outcome==GY_BLUE_WIN?1:game.turn)),0);
    lv_snprintf(value,sizeof(value),"红方 %d 金币    蓝方 %d 金币",game.coins[0],game.coins[1]);
    lv_label_set_text(money_label,value);
    {int displayed=ready&&!placing?selected_count:amount;
    lv_snprintf(value,sizeof(value),"%d 个",displayed);lv_label_set_text(count_label,value);}
    for(y=0;y<6;++y)for(x=0;x<6;++x) {
        const GyCell *c=&shown->cells[y][x];
        int changed=ready&&!placing&&memcmp(c,&game.cells[y][x],sizeof(*c));
        uint32_t bg=(x+y)%2?0x25344b:0x202e44;
        lv_obj_set_style_border_width(tiles[y][x],changed?2:0,0);
        lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(0x61e0c2),0);
        if(placing&&y==(game.turn?0:5)) {
            lv_obj_set_style_border_width(tiles[y][x],2,0);
            lv_obj_set_style_border_color(tiles[y][x],lv_color_hex(place_column==x?0xffdc79:0x65768f),0);
        }
        lv_obj_add_flag(health[y][x],LV_OBJ_FLAG_HIDDEN);
        if(c->kind==GY_SOLDIER) {
            bg=color(c->owner);lv_snprintf(value,sizeof(value),"%c%d",c->owner?'B':'R',c->hp);
            lv_obj_remove_flag(health[y][x],LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(health[y][x],c->hp,LV_ANIM_OFF);
        } else if(c->kind==GY_NUMBER) {
            bg=0xc6a66b;lv_snprintf(value,sizeof(value),"%u",c->value);
        } else value[0]=0;
        lv_obj_set_style_bg_color(tiles[y][x],lv_color_hex(bg),0);
        lv_label_set_text(labels[y][x],value);
        lv_obj_set_style_text_color(labels[y][x],lv_color_hex(c->kind==GY_EMPTY?0xffffff:0x142238),0);
        lv_obj_align(labels[y][x],LV_ALIGN_CENTER,0,c->kind==GY_SOLDIER?-3:0);
    }
    if(game.outcome)status("本局结束 请重开");
    else if(ai_thinking)status("AI 思考中...");
    else if(ai_preview_pending)status("AI 预览 即将落子");
    else if(placing)status(place_column<0?"放置 请选择己方底线空位":"已选位置 请确认或取消");
    else if(ready) {
        lv_snprintf(value,sizeof(value),"预览 %s %d 个 确认后移动",dirs[direction],selected_count);status(value);
    }else if(ai_side>=0)status(game.turn==ai_side?"AI 回合":"先选方向 再确认");
    if(ready&&human_turn())lv_obj_remove_state(confirm_button,LV_STATE_DISABLED);
    else lv_obj_add_state(confirm_button,LV_STATE_DISABLED);
}
static void start_ai(void)
{
    GyAiConfig cfg;
    if(ai_side<0||!playing||game.outcome!=GY_PLAYING||game.turn!=ai_side||
       ai_thinking||ai_preview_pending)return;
    cfg=gy_ai_default_config();
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
void gy_ui_action(int a)
{
    uint32_t old_revision=game.revision;
    int old_outcome=game.outcome;
    if(a==GY_UI_HOME){ai_side=-1;feedback=GY_FX_CLEAR;show_page(home_page);return;}
    if(a==GY_UI_MODES){ai_side=-1;feedback=GY_FX_CLEAR;show_page(mode_page);return;}
    if(a==GY_UI_SINGLE){ai_side=-1;show_page(game_page);a=GY_UI_RESTART;}
    if(a==GY_UI_AI){
        ai_side=-1;prep_human_red=1;update_prep_labels();
        feedback=GY_FX_CLEAR;show_page(prep_page);return;
    }
    if(a==GY_UI_PREP_SWAP){
        if(!prep_active)return;
        prep_human_red=!prep_human_red;update_prep_labels();return;
    }
    if(a==GY_UI_PREP_CANCEL){ai_side=-1;feedback=GY_FX_CLEAR;show_page(mode_page);return;}
    if(a==GY_UI_PREP_START){
        ai_side=prep_human_red?1:0;show_page(game_page);a=GY_UI_RESTART;
    }
    if(!playing)return;
    if(a==GY_UI_RESTART) {
        cancel_ai();
        feedback=GY_FX_CLEAR;
        gy_init(&game,gy_default_config(),20261015);direction=-1;amount=1;placing=0;ready=0;place_column=-1;
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
        else status("需要4金币才能放置");
    } else if(a==GY_UI_RESIGN) {gy_resign(&game);ready=0;placing=0;}
    if(!old_outcome&&game.outcome)
        feedback=game.outcome==GY_RED_WIN?GY_FX_RED:game.outcome==GY_BLUE_WIN?GY_FX_BLUE:GY_FX_DRAW;
    else if(a==GY_UI_CONFIRM&&game.revision!=old_revision)feedback=GY_FX_MOVE;
    render();
    if(!game.outcome&&ai_side>=0&&game.turn==ai_side)start_ai();
}
void gy_ui_cell(int x,int y)
{
    if(!playing||ai_thinking||!human_turn()||!placing||game.outcome||x<0||x>=6||y!=(game.turn?0:5))return;
    place_column=x;ready=game.cells[y][x].kind==GY_EMPTY;render();
    if(!ready)status("位置已被占用 请选择空位");
}
static void action_event(lv_event_t *e) {gy_ui_action((int)(intptr_t)lv_event_get_user_data(e));}
static void cell_event(lv_event_t *e)
{int n=(int)(intptr_t)lv_event_get_user_data(e);gy_ui_cell(n%6,n/6);}
static lv_obj_t *button(lv_obj_t *root,const char *name,int x,int y,int w,int h,int action)
{
    lv_obj_t *b=lv_button_create(root);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_set_style_bg_color(b,lv_color_hex(action==GY_UI_CONFIRM?0x167d73:0x344760),0);
    lv_obj_set_style_radius(b,7,0);lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_t *label=lv_label_create(b);lv_label_set_text(label,name);lv_obj_center(label);
    lv_obj_add_event_cb(b,action_event,LV_EVENT_CLICKED,(void*)(intptr_t)action);return b;
}
static lv_obj_t *side_box(lv_obj_t *root,const char *name,int x,int y,int w,int h,
                          int action,uint32_t bg,lv_obj_t **label_out)
{
    lv_obj_t *b=lv_button_create(root);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_set_style_bg_color(b,lv_color_hex(bg),0);
    lv_obj_set_style_radius(b,10,0);lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_t *label=lv_label_create(b);lv_label_set_text(label,name);lv_obj_center(label);
    lv_obj_set_style_text_color(label,lv_color_hex(0x142238),0);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_add_event_cb(b,action_event,LV_EVENT_CLICKED,(void*)(intptr_t)action);
    if(label_out)*label_out=label;
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
    int x,y;lv_obj_t *root=lv_screen_active();
    lv_obj_set_style_bg_color(root,lv_color_hex(0x111e31),0);
    lv_obj_set_style_text_font(root,&geyi_font_cn16,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    home_page=page(root);mode_page=page(root);game_page=page(root);prep_page=page(root);
    {lv_obj_t *t=text(home_page,"GEYI",32,46);lv_obj_set_style_text_font(t,&lv_font_montserrat_24,0);}
    text(home_page,"格弈 / 合成攻防",32,92);
    text(home_page,"倾斜棋盘  轮流对战",32,123);
    button(home_page,"开始游戏",32,188,326,52,GY_UI_MODES);
    unavailable(home_page,"自定义游戏参数 - 暂未开放",258);
    unavailable(home_page,"历史记录 - 暂未开放",324);
    text(home_page,"单机 / 人机 原型",32,406);
    text(mode_page,"选择游戏模式",32,36);
    text(mode_page,"单机或人机，开始一局对战",32,70);
    button(mode_page,"单机对战",32,104,326,44,GY_UI_SINGLE);
    text(mode_page,"两位玩家轮流操作一块开发板",40,152);
    button(mode_page,"人机对战",32,178,326,44,GY_UI_AI);
    text(mode_page,"进入后选择先后手，AI 自动走子",40,228);
    unavailable(mode_page,"联机对战 - 暂未开放",260);
    button(mode_page,"返回首页",32,382,326,40,GY_UI_HOME);
    text(prep_page,"人机对战",32,36);
    text(prep_page,"选择先后手",32,70);
    side_box(prep_page,"红方\n先手\n玩家",32,118,150,170,
             GY_UI_PREP_SWAP,0xff6b7c,&prep_red_label);
    side_box(prep_page,"蓝方\n后手\nAI",208,118,150,170,
             GY_UI_PREP_SWAP,0x5ba8ff,&prep_blue_label);
    text(prep_page,"点击方框交换先后手",32,300);
    button(prep_page,"取消",32,350,150,44,GY_UI_PREP_CANCEL);
    button(prep_page,"开始游戏",208,350,150,44,GY_UI_PREP_START);
    update_prep_labels();
    root=game_page;
    lv_obj_t *title=text(root,"GEYI",16,8);lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);
    button(root,"首页",138,8,59,28,GY_UI_HOME);
    button(root,"重开",205,8,78,28,GY_UI_RESTART);button(root,"认输",291,8,82,28,GY_UI_RESIGN);
    turn_label=text(root,"RED TURN",16,40);money_label=text(root,"",16,61);
    for(y=0;y<6;++y)for(x=0;x<6;++x) {
        lv_obj_t *tile=lv_obj_create(root);tiles[y][x]=tile;
        lv_obj_set_pos(tile,76+x*40,86+y*40);lv_obj_set_size(tile,37,37);
        lv_obj_set_style_radius(tile,5,0);lv_obj_set_style_pad_all(tile,0,0);
        lv_obj_remove_flag(tile,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tile,cell_event,LV_EVENT_CLICKED,(void*)(intptr_t)(y*6+x));
        labels[y][x]=lv_label_create(tile);lv_obj_set_style_text_font(labels[y][x],&lv_font_montserrat_18,0);
        health[y][x]=lv_bar_create(tile);lv_obj_set_size(health[y][x],27,4);lv_obj_align(health[y][x],LV_ALIGN_BOTTOM_MID,0,-3);
        lv_bar_set_range(health[y][x],0,8);lv_obj_set_style_bg_color(health[y][x],lv_color_hex(0xffffff),LV_PART_INDICATOR);
        lv_obj_remove_flag(health[y][x],LV_OBJ_FLAG_CLICKABLE);
    }
    button(root,LV_SYMBOL_UP,16,339,40,30,GY_UI_UP);button(root,LV_SYMBOL_RIGHT,62,339,40,30,GY_UI_RIGHT);
    button(root,LV_SYMBOL_DOWN,108,339,40,30,GY_UI_DOWN);button(root,LV_SYMBOL_LEFT,154,339,40,30,GY_UI_LEFT);
    button(root,"-",210,339,32,30,GY_UI_LESS);count_label=text(root,"",248,348);
    button(root,"+",344,339,32,30,GY_UI_MORE);
    confirm_button=button(root,"确认",16,378,115,32,GY_UI_CONFIRM);
    lv_obj_set_style_opa(confirm_button,LV_OPA_40,LV_STATE_DISABLED);
    button(root,"取消",139,378,110,32,GY_UI_CANCEL);button(root,"放置",257,378,118,32,GY_UI_PLACE);
    hint=text(root,"",16,422);lv_obj_set_width(hint,360);
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
int gy_ui_take_feedback(void) {int event=feedback;feedback=GY_FX_NONE;return event;}
void gy_ui_tick(void)
{
    GyAiResult r;
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
    if(!playing||ai_thinking||!human_turn()||placing||game.outcome)return;
    direction=d;selected_count=count;amount=count?count:1;
    ready=gy_preview(&game,m,&preview);
    if(!ready)status("方向不可用 请重新选择");
    render();
}
#endif
