#ifdef GEYI_WITH_LVGL
#include "geyi_ui.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static GyGame game;
static GyPreview preview;
static int direction=-1, amount=1, selected_count=1, placing=0, place_column=-1, ready=0;
static lv_obj_t *tiles[6][6], *labels[6][6], *health[6][6];
static lv_obj_t *turn_label,*money_label,*hint,*count_label,*confirm_button;
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
    const char *outcomes[]={"","赤方勝","青方勝","平局"};
    const char *dirs[]={"上","右","下","左"};
    lv_label_set_text(turn_label,game.outcome?outcomes[game.outcome]:(game.turn?"青方回合":"赤方回合"));
    lv_obj_set_style_text_color(turn_label,lv_color_hex(game.outcome==GY_DRAW?0xffdc79:
        color(game.outcome==GY_RED_WIN?0:game.outcome==GY_BLUE_WIN?1:game.turn)),0);
    lv_snprintf(value,sizeof(value),"赤方 %d 金    青方 %d 金",game.coins[0],game.coins[1]);
    lv_label_set_text(money_label,value);
    {int displayed=ready&&!placing?selected_count:amount;
    lv_snprintf(value,sizeof(value),"%d 個",displayed);lv_label_set_text(count_label,value);}
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
    if(game.outcome)status("本局結束 請重開");
    else if(placing)status(place_column<0?"放置 請選己方底線空位":"已選位置 請確認或取消");
    else if(ready) {
        lv_snprintf(value,sizeof(value),"選擇中 %s %d 個 確認才移動",dirs[direction],selected_count);status(value);
    }
    if(ready)lv_obj_remove_state(confirm_button,LV_STATE_DISABLED);
    else lv_obj_add_state(confirm_button,LV_STATE_DISABLED);
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
    if(!ready)status("方向不可用 請重新選擇");
}
void gy_ui_action(int a)
{
    if(a==GY_UI_RESTART) {
        gy_init(&game,gy_default_config(),20261015);direction=-1;amount=1;placing=0;ready=0;place_column=-1;
        status("先選方向 再確認");
    } else if(a==GY_UI_CANCEL) {
        ready=0;placing=0;place_column=-1;direction=-1;status("已取消 本局狀態不變");
    } else if(game.outcome) {render();return;}
    else if(a<=GY_UI_LEFT) {placing=0;direction=a;choose();}
    else if(a==GY_UI_LESS||a==GY_UI_MORE) {
        amount+=a==GY_UI_MORE?1:-1;if(amount<1)amount=1;if(amount>36)amount=36;choose();
    } else if(a==GY_UI_CONFIRM) {
        int ok=ready&&(placing?gy_place(&game,place_column):gy_commit(&game,&preview));
        ready=0;placing=0;direction=-1;status(ok?"回合結束 請交給對方":"沒有可確認的選擇");
    } else if(a==GY_UI_PLACE) {
        ready=0;placing=0;place_column=-1;
        if(game.coins[game.turn]>=game.config.cost)placing=1;
        else status("需要4金才能放置");
    } else if(a==GY_UI_RESIGN) {gy_resign(&game);ready=0;placing=0;}
    render();
}
void gy_ui_cell(int x,int y)
{
    if(!placing||game.outcome||x<0||x>=6||y!=(game.turn?0:5))return;
    place_column=x;ready=game.cells[y][x].kind==GY_EMPTY;render();
    if(!ready)status("此位置已有單位 請選空位");
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
void gy_ui_create(void)
{
    int x,y;lv_obj_t *root=lv_screen_active();
    lv_obj_set_style_bg_color(root,lv_color_hex(0x111e31),0);
    lv_obj_set_style_text_font(root,&lv_font_source_han_sans_sc_16_cjk,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title=text(root,"GEYI",16,8);lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);
    button(root,"重開",205,8,78,28,GY_UI_RESTART);button(root,"認輸",291,8,82,28,GY_UI_RESIGN);
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
    confirm_button=button(root,"確認",16,378,115,32,GY_UI_CONFIRM);
    lv_obj_set_style_opa(confirm_button,LV_OPA_40,LV_STATE_DISABLED);
    button(root,"取消",139,378,110,32,GY_UI_CANCEL);button(root,"放置",257,378,118,32,GY_UI_PLACE);
    hint=text(root,"",16,422);lv_obj_set_width(hint,360);
    gy_ui_action(GY_UI_RESTART);
}
const GyGame *gy_ui_game(void) {return &game;}
int gy_ui_has_preview(void) {return ready;}
#endif
