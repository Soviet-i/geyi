#include "geyi_input.h"
#include <stdlib.h>
#include <string.h>

void gy_input_reset(GyInput *s)
{
    memset(s,0,sizeof(*s));
    s->direction = s->pending_direction = -1;
    /* 默认 10%，与改造前的硬编码一致；调用方随后可用 gy_input_configure() 覆盖 */
    s->dir_buffer_pct = GY_INPUT_BUFFER_DEFAULT;
    s->angle_buffer_pct = GY_INPUT_BUFFER_DEFAULT;
}
static int clamp_pct(int v)
{
    if(v < GY_INPUT_BUFFER_MIN) return GY_INPUT_BUFFER_MIN;
    if(v > GY_INPUT_BUFFER_MAX) return GY_INPUT_BUFFER_MAX;
    return v;
}
void gy_input_configure(GyInput *s, int dir_pct, int angle_pct)
{
    if(!s) return;
    s->dir_buffer_pct = clamp_pct(dir_pct);
    s->angle_buffer_pct = clamp_pct(angle_pct);
}
int gy_input_begin_session(GyInput *s, uint32_t session, uint32_t *last_session)
{
    if(!s||!last_session||session==*last_session) return 0;
    *last_session=session;
    gy_input_reset(s);
    return 1;
}
static void clear_selection(GyInput *s, GyInputEvent *e)
{
    if(s->direction>=0) e->events |= GY_INPUT_CANCEL;
    s->direction=s->pending_direction=-1;s->count=0;
    s->available=0;s->pending_available=0;
    s->aim=0;s->aim_since=0;s->exit_since=0;
}
/* 确认一次：清空可见选择并锁定；直接喂 key 的调用者需要先回中，
 * 板端 KEY2 在 UI 层，换回合 reset 后允许重新预览（见 docs/02）。 */
static void apply_confirm(GyInput *s, GyInputEvent *e)
{
    e->events|=GY_INPUT_CONFIRM;
    e->direction=s->direction;e->count=s->count;e->aim=s->aim;
    s->locked=1;s->direction=-1;s->count=0;s->available=0;s->aim=0;
    s->pending_direction=-1;s->pending_count=0;s->pending_available=0;
}
int gy_input_direction(int x, int y)
{
    int ax=abs(x),ay=abs(y);
    if(ax==0&&ay==0) return -1;
    return ax>ay?(x>0?GY_RIGHT:GY_LEFT):(y>0?GY_DOWN:GY_UP);
}
int gy_input_aim(int x, int y, int direction)
{
    int ax=abs(x),ay=abs(y),magnitude=ax>ay?ax:ay,lateral;
    if(magnitude<=0||direction<0||direction>3) return 0;
    lateral=(direction==GY_LEFT||direction==GY_RIGHT)?y:x;
    lateral=lateral*100/magnitude;
    if(lateral<-100)lateral=-100;
    if(lateral>100)lateral=100;
    return lateral;
}
/* 数量档位：从 6° 起每 10° 一档；N 档放不下（最后一档超过 60°）时按 N 等分。
 * ref_count 只用于档位边界回差，避免手抖跨档时来回跳。 */
static int band_count(int magnitude, int n, int ref_count, int angle_pct)
{
    int span = GY_INPUT_MAX_MAG - GY_INPUT_ENTER_MAG;
    int band, margin, count;
    if(n <= 0) return 0;
    if(magnitude > GY_INPUT_MAX_MAG) magnitude = GY_INPUT_MAX_MAG;
    if((n - 1) * GY_INPUT_BAND_MAG <= span) band = GY_INPUT_BAND_MAG;
    else band = span / n;
    if(band < 1) band = 1;
    margin = band * angle_pct / 100;
    count = ref_count;
    if(count < 1) count = 1;
    if(count > n) count = n;
    while(count < n && magnitude >= GY_INPUT_ENTER_MAG + count * band + margin) ++count;
    while(count > 1 && magnitude <  GY_INPUT_ENTER_MAG + (count - 1) * band - margin) --count;
    return count;
}
GyInputEvent gy_input_update(GyInput *s,int x,int y,int key,uint32_t now,
                             const int soldiers[4],unsigned legal_mask)
{
    GyInputEvent e={0,-1,0,0};
    int ax,ay,d,magnitude,n,count,aim,press=0,i,ref_dir,ref_count,had_selection,updated=0;
    if(!s) return e;
    key=!!key;
    if(!soldiers || (legal_mask & ~0xFu) ||
       x < -9000 || x > 9000 || y < -9000 || y > 9000) {
        clear_selection(s,&e);s->started=0;s->locked=0;return e;
    }
    for(i=0;i<4;++i) if(soldiers[i]<0||soldiers[i]>GY_MAX*GY_MAX) {
        clear_selection(s,&e);s->started=0;s->locked=0;return e;
    }
    if(!s->started) {
        /* 新一段输入（开局/换回合/首次样本）：清掉旧选择，但不锁存。
         * 传感器是否中断由板级 gy_board_poll 的 seq 新鲜度负责，输入层
         * 不再用主循环两次调用的时间差判死，避免触摸/重绘停顿造成闪断。 */
        clear_selection(s,&e);s->raw_key=s->stable_key=key;
        s->key_since=now;s->armed=0;s->started=1;s->locked=0;
        return e;
    }
    if(key!=s->raw_key) {s->raw_key=key;s->key_since=now;}
    if((uint32_t)(now-s->key_since)>=20) {
        if(!key) s->armed=1;
        if(key!=s->stable_key) {
            s->stable_key=key;
            if(key&&s->armed) {press=1;s->armed=0;}
        }
    }
    ax=abs(x);ay=abs(y);magnitude=ax>ay?ax:ay;
    if(magnitude<GY_INPUT_EXIT_MAG) {
        /* 回中才取消；6°~4° 之间保持已有预览，低于 4° 也要持续
         * GY_INPUT_EXIT_HOLD_MS 才清除，避免主线程/触摸卡顿或第二局
         * 起始附近在 4°/6° 门限来回跳时预览闪断。 */
        if(s->direction<0){clear_selection(s,&e);s->locked=0;s->exit_since=0;return e;}
        if(!s->exit_since)s->exit_since=now?now:1;
        if((uint32_t)(now-s->exit_since)<GY_INPUT_EXIT_HOLD_MS){
            e.direction=s->direction;e.count=s->count;e.aim=s->aim;return e;
        }
        clear_selection(s,&e);s->locked=0;s->exit_since=0;return e;
    }
    s->exit_since=0;
    /* 确认后先回中：在 4° 以上不再自动重新预览，防止跨回合连发。 */
    if(s->locked) return e;
    if(magnitude<GY_INPUT_ENTER_MAG) {
        /* 4°~6° 回差带：保持已有方向/数量，不再换向；仍可直接确认。 */
        if(s->direction<0) {
            s->pending_direction=-1;s->pending_count=0;s->pending_available=0;
            s->available=0;s->pending_since=now;return e;
        }
        if(soldiers[s->direction]!=s->available) {clear_selection(s,&e);return e;}
        if(press) {apply_confirm(s,&e);return e;}
        e.direction=s->direction;e.count=s->count;e.aim=s->aim;return e;
    }
    d=gy_input_direction(x,y);
    if(d<0) {clear_selection(s,&e);return e;}
    /* 保持原方向，直到竞争轴比它强出 dir_buffer_pct%（默认 10%，见 geyi_config）。 */
    ref_dir=s->direction>=0?s->direction:s->pending_direction;
    if(ref_dir>=0&&d!=ref_dir) {
        int oldmag=(ref_dir==GY_LEFT||ref_dir==GY_RIGHT)?ax:ay;
        int oldsign=(ref_dir==GY_LEFT?x<0:ref_dir==GY_RIGHT?x>0:ref_dir==GY_UP?y<0:y>0);
        if(oldsign&&magnitude*100<=oldmag*(100+s->dir_buffer_pct)) d=ref_dir;
    }
    if(!(legal_mask&(1u<<d))) {
        /* 该方向没有任何有效走法：取消旧预览，避免确认旧方向。 */
        if(s->direction>=0) clear_selection(s,&e);
        else {
            s->pending_direction=-1;s->pending_count=0;s->pending_available=0;
            s->pending_since=now;
        }
        return e;
    }
    n=soldiers[d];
    aim=gy_input_aim(x,y,d);
    if(s->direction==d&&n!=s->available) {
        /* 可见方向的可行动兵数变了：旧预览立即作废并重新稳定。 */
        clear_selection(s,&e);
        s->pending_direction=d;s->pending_count=band_count(magnitude,n,1,s->angle_buffer_pct);
        s->pending_available=n;s->pending_since=now;return e;
    }
    if(s->direction==d) ref_count=s->count;
    else if(s->pending_direction==d) ref_count=s->pending_count;
    else ref_count=1;
    count=band_count(magnitude,n,ref_count,s->angle_buffer_pct);
    had_selection=s->direction>=0;
    if(!had_selection) {
        if(d!=s->pending_direction||count!=s->pending_count||n!=s->pending_available) {
            s->pending_direction=d;s->pending_count=count;s->pending_available=n;
            s->pending_since=now;return e;
        }
        if((uint32_t)(now-s->pending_since)<GY_INPUT_STABLE_MS) return e;
        s->direction=d;s->count=count;s->available=n;s->aim=aim;
        s->aim_since=now;
        e.events|=GY_INPUT_SELECT;e.direction=d;e.count=count;e.aim=aim;
        /* 选择刚出现时按下的键不算确认（按 KEY2 前必须先看到预览）。 */
        return e;
    }
    if(d==s->direction&&count==s->count&&n==s->available) {
        /* 候选与可见选择一致：刷新待稳定起点，单次抖动不会累积。
         * aim 不参与 200ms 稳定判据：横向 0.1° 级噪声不能把预览永久卡住；
         * 只有变化达到 10% 才立即更新方位（同一方向/数量）。 */
        s->pending_direction=d;s->pending_count=count;s->pending_available=n;
        s->pending_since=now;
        /* aim-only 更新单独稳定 200ms：30↔50 的往复抖动不能每帧触发
         * UPDATE/翻转；持续 200ms 才真正改变方位。 */
        if(abs(aim-s->aim)>=10) {
            /* 连续缓慢转动时 aim 每帧都在变，不能要求“aim 完全相同”200ms；
             * 只要持续偏离当前方位超过阈值，计时就保留，到点后切到最新 aim。 */
            if(!s->aim_since) s->aim_since=now?now:1;
            if((uint32_t)(now-s->aim_since)>=GY_INPUT_STABLE_MS){
                s->aim=aim;e.events|=GY_INPUT_UPDATE;updated=1;
                s->aim_since=0;
            }
        }else{
            s->aim_since=0;
        }
    }else{
        if(d!=s->pending_direction||count!=s->pending_count||n!=s->pending_available) {
            s->pending_direction=d;s->pending_count=count;s->pending_available=n;
            s->pending_since=now;return e;
        }
        if((uint32_t)(now-s->pending_since)<GY_INPUT_STABLE_MS) return e;
        /* 新选择稳定：在旧预览上直接切换，不发 CANCEL。 */
        s->direction=d;s->count=count;s->available=n;s->aim=aim;
        s->aim_since=now;
        e.events|=GY_INPUT_UPDATE;updated=1;
    }
    /* 同一帧刚发生 UPDATE 时不确认，避免用户按下的还是旧选择。 */
    if(press&&s->direction>=0&&!updated) apply_confirm(s,&e);
    e.direction=s->direction;e.count=s->count;e.aim=s->aim;
    return e;
}
int gy_input_place_cell(const GyGame *g,int x,int y,
                        int current_row,int current_column,GyInputPlace *out)
{
    int size,d,aim,span,pos,row,col,dr=0,dc=0,steps;
    if(!g||!out) return 0;
    size=g->config.size;
    if(size<4||size>GY_MAX) return 0;
    if(current_row>=0&&current_row<size&&current_column>=0&&current_column<size){
        if(abs(x)<GY_INPUT_PLACE_DEAD&&abs(y)<GY_INPUT_PLACE_DEAD&&
           g->cells[current_row][current_column].kind==GY_EMPTY){
            out->row=current_row;out->column=current_column;return 1;
        }
    }
    d=gy_input_direction(x,y);
    if(d<0){
        if(current_row>=0&&current_row<size&&current_column>=0&&current_column<size){
            out->row=current_row;out->column=current_column;return 1;
        }
        return 0;
    }
    aim=gy_input_aim(x,y,d);
    span=size-1;
    /* 0..span：把方位比例映射到外侧边的横向格坐标；+100 为正方向末格。 */
    pos=(span*(aim+100)+100)/200;
    if(pos<0)pos=0;
    if(pos>span)pos=span;
    switch(d){
    case GY_UP:    row=0;col=pos;dr=1;break;
    case GY_DOWN:  row=size-1;col=pos;dr=-1;break;
    case GY_LEFT:  row=pos;col=0;dc=1;break;
    default:       row=pos;col=size-1;dc=-1;break;
    }
    /* 目标被占时沿同一行/列向内逐格找第一个空格。 */
    for(steps=0;steps<size*2;++steps){
        if(row<0||row>=size||col<0||col>=size)break;
        if(g->cells[row][col].kind==GY_EMPTY){
            out->row=row;out->column=col;return 1;
        }
        row+=dr;col+=dc;
    }
    return 0;
}
int gy_input_selftest(void)
{
    GyInput s;GyInputEvent e;int a[4]={2,2,2,2};unsigned legal=0xF;uint32_t t;
    gy_input_reset(&s);
    for(t=0;t<=30;t+=5) gy_input_update(&s,0,0,0,t,a,legal);
    for(;t<=240;t+=5) e=gy_input_update(&s,0,-1000,0,t,a,legal);
    if(s.direction!=GY_UP||s.count!=1)return 0;
    for(;t<=275;t+=5) {
        e=gy_input_update(&s,0,-1000,1,t,a,legal);
        if(e.events&GY_INPUT_CONFIRM)return 1;
    }
    return 0;
}
