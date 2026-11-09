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
static void clear_selection(GyInput *s, GyInputEvent *e)
{
    if(s->direction>=0) e->events |= GY_INPUT_CANCEL;
    s->direction=s->pending_direction=-1;s->count=0;
    s->available=0;s->pending_available=0;
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
GyInputEvent gy_input_update(GyInput *s,int x,int y,int key,uint32_t now,const int available[4])
{
    GyInputEvent e={0,-1,0};
    int ax,ay,d,magnitude,n,count,press=0,i,ref_dir,ref_count,had_selection;
    if(!s) return e;
    key=!!key;
    if(!available || x < -9000 || x > 9000 || y < -9000 || y > 9000) {
        clear_selection(s,&e);s->started=0;s->locked=0;return e;
    }
    for(i=0;i<4;++i) if(available[i]<0||available[i]>GY_MAX*GY_MAX) {
        clear_selection(s,&e);s->started=0;s->locked=0;return e;
    }
    if(!s->started || (uint32_t)(now-s->last_ms)>100) {
        /* 新一段输入（开局/换回合/样本中断）：清掉旧选择，但不锁存。
         * 轮到自己时板子已经斜着，下一段样本稳定 200ms 后仍应进入预览。 */
        clear_selection(s,&e);s->raw_key=s->stable_key=key;
        s->key_since=now;s->armed=0;s->started=1;s->locked=0;
        s->last_ms=now;return e;
    }
    s->last_ms=now;
    if(key!=s->raw_key) {s->raw_key=key;s->key_since=now;}
    if((uint32_t)(now-s->key_since)>=20) {
        if(!key) s->armed=1;
        if(key!=s->stable_key) {
            s->stable_key=key;
            if(key&&s->armed) {press=1;s->armed=0;}
        }
    }
    ax=abs(x);ay=abs(y);magnitude=ax>ay?ax:ay;
    if(magnitude<=GY_INPUT_EXIT_MAG) {
        /* 回中才取消；6°~4° 之间保持已有预览，避免手抖闪断。 */
        clear_selection(s,&e);s->locked=0;return e;
    }
    /* 确认后先回中：在 4° 以上不再自动重新预览，防止跨回合连发。 */
    if(s->locked) return e;
    if(magnitude<GY_INPUT_ENTER_MAG&&s->direction<0) {
        s->pending_direction=-1;s->pending_count=0;s->available=0;
        s->pending_since=now;return e;
    }
    d=ax>ay?(x>0?GY_RIGHT:GY_LEFT):(y>0?GY_DOWN:GY_UP);
    /* 保持原方向，直到竞争轴比它强出 dir_buffer_pct%（默认 10%，见 geyi_config）。 */
    ref_dir=s->direction>=0?s->direction:s->pending_direction;
    if(ref_dir>=0&&d!=ref_dir) {
        int oldmag=(ref_dir==GY_LEFT||ref_dir==GY_RIGHT)?ax:ay;
        int oldsign=(ref_dir==GY_LEFT?x<0:ref_dir==GY_RIGHT?x>0:ref_dir==GY_UP?y<0:y>0);
        if(oldsign&&magnitude*100<=oldmag*(100+s->dir_buffer_pct)) d=ref_dir;
    }
    n=available[d];
    if(n==0) {
        /* 候选方向没有可行动兵：已有可见选择时先保留，等方向真正切过去再处理。 */
        if(s->direction==d) clear_selection(s,&e);
        return e;
    }
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
        s->direction=d;s->count=count;s->available=n;
        e.events|=GY_INPUT_SELECT;e.direction=d;e.count=count;
        /* 选择刚出现时按下的键不算确认（按 KEY2 前必须先看到预览）。 */
        return e;
    }
    if(d==s->direction&&count==s->count&&n==s->available) {
        /* 候选与可见选择一致：刷新待稳定起点，单次抖动不会累积。 */
        s->pending_direction=d;s->pending_count=count;s->pending_available=n;
        s->pending_since=now;
    }else{
        if(d!=s->pending_direction||count!=s->pending_count||n!=s->pending_available) {
            s->pending_direction=d;s->pending_count=count;s->pending_available=n;
            s->pending_since=now;return e;
        }
        if((uint32_t)(now-s->pending_since)<GY_INPUT_STABLE_MS) return e;
        /* 新选择稳定：在旧预览上直接切换，不发 CANCEL。 */
        s->direction=d;s->count=count;s->available=n;
        e.events|=GY_INPUT_UPDATE;
    }
    if(press&&s->direction>=0) {
        e.events|=GY_INPUT_CONFIRM;
        e.direction=s->direction;e.count=s->count;
        /* 确认后清掉可见选择并锁定，直到回中（低于 4°）。 */
        s->locked=1;s->direction=-1;s->count=0;s->available=0;
        s->pending_direction=-1;s->pending_count=0;s->pending_available=0;
        return e;
    }
    e.direction=s->direction;e.count=s->count;
    return e;
}
int gy_input_place_column(const GyGame *g,int owner,int x,int y,int current)
{
    int size,row,twice,target,d;
    (void)y;
    if(!g) return -1;
    size=g->config.size;
    if(size<4||size>GY_MAX) return -1;
    row=owner==0?size-1:0;
    if(abs(x)<GY_INPUT_PLACE_DEAD&&current>=0&&current<size) {
        target=current;
    }else{
        /* 双倍列坐标：列中心 0..size-1 映射到 ±(size-1)。 */
        twice=(size-1)+(int)((long)x*(size-1)/GY_INPUT_PLACE_FULL);
        target=twice>=0?(twice+1)/2:twice/2;
        if(target<0)target=0;
        if(target>=size)target=size-1;
    }
    for(d=0;d<size;++d) {
        int a=target-d,b=target+d;
        if(a>=0&&g->cells[row][a].kind==GY_EMPTY) return a;
        if(d&&b<size&&g->cells[row][b].kind==GY_EMPTY) return b;
    }
    return -1;
}
int gy_input_selftest(void)
{
    GyInput s;GyInputEvent e;int a[4]={2,2,2,2};uint32_t t;
    gy_input_reset(&s);
    for(t=0;t<=30;t+=5) gy_input_update(&s,0,0,0,t,a);
    for(;t<=240;t+=5) e=gy_input_update(&s,0,-1000,0,t,a);
    if(s.direction!=GY_UP||s.count!=1)return 0;
    for(;t<=275;t+=5) {
        e=gy_input_update(&s,0,-1000,1,t,a);
        if(e.events&GY_INPUT_CONFIRM)return 1;
    }
    return 0;
}
