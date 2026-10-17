#include "geyi_input.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int checks;
#define CHECK(x) do {++checks;if(!(x)){printf("FAIL input line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {GyInput input;uint32_t now;int available[4];} Simulation;
static GyInputEvent sample(Simulation *s,int x,int y,int key)
{s->now+=5;return gy_input_update(&s->input,x,y,key,s->now,s->available);}
static int hold(Simulation *s,int x,int y,int key,int ticks)
{int events=0;while(ticks-->0)events|=sample(s,x,y,key).events;return events;}
static Simulation setup(void)
{Simulation s;int d;memset(&s,0,sizeof(s));gy_input_reset(&s.input);for(d=0;d<4;++d)s.available[d]=2;return s;}
int main(void)
{
    Simulation s;GyInputEvent event;int d,i,confirms;
    CHECK(gy_input_selftest());
    for(d=0;d<4;++d){
        int x=d==GY_RIGHT?2000:d==GY_LEFT?-2000:0;
        int y=d==GY_DOWN?2000:d==GY_UP?-2000:0;
        s=setup();hold(&s,0,0,0,8);
        CHECK(!(hold(&s,x,y,0,39)&GY_INPUT_SELECT));
        CHECK(hold(&s,x,y,0,3)&GY_INPUT_SELECT);
        CHECK(s.input.direction==d&&s.input.count==1);
        CHECK(hold(&s,x,y,1,6)&GY_INPUT_CONFIRM);
        CHECK(!(hold(&s,x,y,1,100)&GY_INPUT_CONFIRM));
        hold(&s,x,y,0,8);CHECK(!(hold(&s,x,y,1,8)&GY_INPUT_CONFIRM));
        hold(&s,0,0,0,8);CHECK(hold(&s,x,y,0,42)&GY_INPUT_SELECT);
        CHECK(hold(&s,x,y,1,8)&GY_INPUT_CONFIRM);
    }
    s=setup();hold(&s,0,0,0,8);hold(&s,0,-2000,0,42);
    /* Stable release, bouncing down/up cannot generate multiple presses. */
    confirms=0;
    for(i=0;i<8;++i)confirms+=(sample(&s,0,-2000,i%2).events&GY_INPUT_CONFIRM)!=0;
    for(i=0;i<12;++i)confirms+=(sample(&s,0,-2000,1).events&GY_INPUT_CONFIRM)!=0;
    CHECK(confirms==1);
    s=setup();hold(&s,0,0,0,8);hold(&s,0,-2000,1,45);
    CHECK(s.input.direction==GY_UP);CHECK(!(hold(&s,0,-2000,1,40)&GY_INPUT_CONFIRM));
    hold(&s,0,-2000,0,8);CHECK(hold(&s,0,-2000,1,8)&GY_INPUT_CONFIRM);
    /* 2 soldiers: 35 degree boundary, 2.5 degree hysteresis. */
    s=setup();hold(&s,0,0,0,8);hold(&s,0,-2000,0,42);
    CHECK(!(hold(&s,0,-3600,0,45)&GY_INPUT_CANCEL));CHECK(s.input.count==1);
    CHECK(hold(&s,0,-3800,0,45)&GY_INPUT_SELECT);CHECK(s.input.count==2);
    hold(&s,0,-3400,0,45);CHECK(s.input.count==2);
    hold(&s,0,-3200,0,45);CHECK(s.input.count==1);
    hold(&s,2000,-2100,0,45);CHECK(s.input.direction==GY_UP);
    hold(&s,2150,-2100,0,45);CHECK(s.input.direction==GY_UP);
    hold(&s,2400,-2100,0,45);CHECK(s.input.direction==GY_RIGHT);
    CHECK(hold(&s,0,0,0,1)&GY_INPUT_CANCEL);CHECK(s.input.direction==-1);
    hold(&s,0,-2000,0,42);s.now+=101;
    event=sample(&s,0,-2000,1);CHECK(event.events&GY_INPUT_CANCEL);
    CHECK(!(hold(&s,0,-2000,1,100)&GY_INPUT_CONFIRM));
    hold(&s,0,0,0,8);hold(&s,0,-2000,0,42);
    CHECK(sample(&s,10000,0,1).events&GY_INPUT_CANCEL);
    CHECK(!(hold(&s,0,-2000,1,100)&GY_INPUT_CONFIRM));
    s=setup();s.now=UINT32_MAX-100;hold(&s,0,0,0,8);
    CHECK(hold(&s,0,-2000,0,45)&GY_INPUT_SELECT);CHECK(hold(&s,0,-2000,1,8)&GY_INPUT_CONFIRM);
    s=setup();hold(&s,0,0,0,8);hold(&s,0,-2000,0,42);s.available[GY_UP]=1;
    CHECK(sample(&s,0,-2000,1).events&GY_INPUT_CANCEL);
    CHECK(!(hold(&s,0,-2000,1,45)&GY_INPUT_CONFIRM));
    /* Feed the real input events to the real preview/commit interfaces. */
    {
        GyGame g,before;GyPreview preview;int ready=0;
        gy_init(&g,gy_default_config(),20261015);before=g;s=setup();
        for(d=0;d<4;++d)s.available[d]=gy_eligible_count(&g,d);
        hold(&s,0,0,0,8);
        for(i=0;i<60;++i){
            event=sample(&s,0,-2000,i>=48);
            if(event.events&GY_INPUT_SELECT){GyMove m={event.direction,event.count};ready=gy_preview(&g,m,&preview);}
            if(event.events&GY_INPUT_CONFIRM){CHECK(ready);CHECK(gy_commit(&g,&preview));}
        }
        CHECK(g.revision==before.revision+1&&g.turn==1);
        CHECK(!(hold(&s,0,-2000,1,100)&GY_INPUT_CONFIRM));
    }
    printf("PASS: input %d assertions; TC-309 simulated angles/time only\n",checks);
    return 0;
}
