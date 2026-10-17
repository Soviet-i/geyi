#include "geyi_input.h"
#include <stdlib.h>
#include <string.h>

void gy_input_reset(GyInput *s)
{
    memset(s,0,sizeof(*s));
    s->direction = s->pending_direction = -1;
}
static void clear_selection(GyInput *s, GyInputEvent *e)
{
    if(s->direction>=0) e->events |= GY_INPUT_CANCEL;
    s->direction=s->pending_direction=-1;s->count=0;s->available=0;
}
GyInputEvent gy_input_update(GyInput *s,int x,int y,int key,uint32_t now,const int available[4])
{
    GyInputEvent e={0,-1,0};
    int ax,ay,d,magnitude,n,count,press=0,i,had_selection;
    if(!s) return e;
    key=!!key;
    if(!available || x < -9000 || x > 9000 || y < -9000 || y > 9000) {
        clear_selection(s,&e);s->armed=0;s->started=0;s->latched=1;return e;
    }
    for(i=0;i<4;++i) if(available[i]<0||available[i]>GY_MAX*GY_MAX) {
        clear_selection(s,&e);s->armed=0;s->started=0;s->latched=1;return e;
    }
    if(!s->started || (uint32_t)(now-s->last_ms)>100) {
        clear_selection(s,&e);s->raw_key=s->stable_key=key;
        s->key_since=now;s->armed=0;s->started=1;
        s->last_ms=now;s->latched=1;return e;
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
    if(magnitude<=1000) {
        clear_selection(s,&e);s->latched=0;return e;
    }
    if(s->latched) return e;
    d=ax>ay?(x>0?GY_RIGHT:GY_LEFT):(y>0?GY_DOWN:GY_UP);
    /* Keep previous sector until the competing axis is >10% stronger. */
    if(s->pending_direction>=0&&d!=s->pending_direction) {
        int old=s->pending_direction;
        int oldmag=(old==GY_LEFT||old==GY_RIGHT)?ax:ay;
        int oldsign=(old==GY_LEFT?x<0:old==GY_RIGHT?x>0:old==GY_UP?y<0:y>0);
        if(oldsign&&magnitude*100<=oldmag*110) d=old;
    }
    n=available[d];
    count=(d==s->pending_direction&&n==s->available)?s->pending_count:0;
    if(magnitude>6000)magnitude=6000;
    /* No soldiers: retain count=0 for a possible number-only move. */
    if(n==0) count=0;
    else {
        int band=5000/n,margin=band/10;
        while(count<n&&magnitude>=1000+count*band+margin) ++count;
        while(count>0&&magnitude<1000+(count-1)*band-margin) --count;
        if(count==0) {clear_selection(s,&e);return e;}
    }
    had_selection=s->direction>=0;
    if(d!=s->pending_direction||count!=s->pending_count||n!=s->available) {
        clear_selection(s,&e);s->pending_direction=d;s->pending_count=count;
        s->available=n;s->pending_since=now;
    }
    if((uint32_t)(now-s->pending_since)<200) return e;
    if(s->direction<0) {
        s->direction=d;s->count=count;e.events|=GY_INPUT_SELECT;
    }
    /* A key pressed before the selection became stable is not confirmation. */
    if(press&&had_selection) {
        e.events|=GY_INPUT_CONFIRM;s->latched=1;
    }
    e.direction=s->direction;e.count=s->count;
    return e;
}
int gy_input_selftest(void)
{
    GyInput s;GyInputEvent e;int a[4]={2,2,2,2};uint32_t t;
    gy_input_reset(&s);
    for(t=0;t<=30;t+=5) gy_input_update(&s,0,0,0,t,a);
    for(;t<=240;t+=5) e=gy_input_update(&s,0,-2000,0,t,a);
    if(s.direction!=GY_UP||s.count!=1)return 0;
    for(;t<=275;t+=5) {
        e=gy_input_update(&s,0,-2000,1,t,a);
        if(e.events&GY_INPUT_CONFIRM)return 1;
    }
    return 0;
}
