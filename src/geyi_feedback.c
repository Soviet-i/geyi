#include "geyi_feedback.h"
void gy_light_start(GyLight *light,int event,uint32_t now)
{
    if(event==GY_FX_NONE)return;
    light->kind=event;light->started=now;
}
uint32_t gy_light_color(const GyLight *light,uint32_t now)
{
    uint32_t elapsed=now-light->started;
    if(light->kind==GY_FX_MOVE)return elapsed<120?0x001800:0;
    if(elapsed>=1800||(elapsed/300)%2)return 0;
    if(light->kind==GY_FX_RED)return 0x180000;
    if(light->kind==GY_FX_BLUE)return 0x000018;
    if(light->kind==GY_FX_DRAW)return 0x181800;
    return 0;
}
int gy_light_selftest(void)
{
    GyLight l={0};unsigned i;
    if(gy_light_color(&l,0))return 0;
    gy_light_start(&l,GY_FX_MOVE,100);
    if(gy_light_color(&l,100)!=0x001800||gy_light_color(&l,219)!=0x001800||gy_light_color(&l,220))return 0;
    for(i=GY_FX_RED;i<=GY_FX_DRAW;++i){
        uint32_t expected=i==GY_FX_RED?0x180000:i==GY_FX_BLUE?0x000018:0x181800;
        unsigned t;gy_light_start(&l,(int)i,0);
        for(t=0;t<1800;++t)if(gy_light_color(&l,t)!=((t/300)%2?0:expected))return 0;
        if(gy_light_color(&l,1800)||gy_light_color(&l,5000))return 0;
    }
    gy_light_start(&l,GY_FX_MOVE,UINT32_MAX-59);
    if(gy_light_color(&l,59)!=0x001800||gy_light_color(&l,60))return 0;
    gy_light_start(&l,GY_FX_NONE,61);
    if(l.started!=UINT32_MAX-59)return 0;
    gy_light_start(&l,GY_FX_CLEAR,62);
    return gy_light_color(&l,62)==0;
}
