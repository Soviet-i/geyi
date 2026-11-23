#include "geyi_input.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int checks;
#define CHECK(x) do {++checks;if(!(x)){printf("FAIL input line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {GyInput input;uint32_t now;int available[4];unsigned legal;} Simulation;
static GyInputEvent sample(Simulation *s,int x,int y,int key)
{s->now+=5;return gy_input_update(&s->input,x,y,key,s->now,s->available,s->legal);}
static int hold(Simulation *s,int x,int y,int key,int ticks)
{int events=0;while(ticks-->0)events|=sample(s,x,y,key).events;return events;}
static Simulation setup(void)
{Simulation s;int d;memset(&s,0,sizeof(s));gy_input_reset(&s.input);for(d=0;d<4;++d)s.available[d]=2;s.legal=0xF;return s;}
int main(void)
{
    Simulation s;GyInputEvent event;int d,i,confirms;
    CHECK(gy_input_selftest());
    for(d=0;d<4;++d){
        int x=d==GY_RIGHT?1000:d==GY_LEFT?-1000:0;
        int y=d==GY_DOWN?1000:d==GY_UP?-1000:0;
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
    /* 2 soldiers: 10-degree bands; 6° entry, 4° exit, 1° band hysteresis. */
    s=setup();hold(&s,0,0,0,8);hold(&s,0,-1000,0,42);
    CHECK(!(hold(&s,0,-1500,0,45)&GY_INPUT_CANCEL));CHECK(s.input.count==1);
    CHECK(hold(&s,0,-1700,0,45)&GY_INPUT_UPDATE);CHECK(s.input.count==2);
    hold(&s,0,-1500,0,45);CHECK(s.input.count==2);
    CHECK(hold(&s,0,-1400,0,45)&GY_INPUT_UPDATE);CHECK(s.input.count==1);
    hold(&s,2000,-2100,0,45);CHECK(s.input.direction==GY_UP);
    hold(&s,2150,-2100,0,45);CHECK(s.input.direction==GY_UP);
    hold(&s,2400,-2100,0,45);CHECK(s.input.direction==GY_RIGHT);
    CHECK(!(hold(&s,0,0,0,10)&GY_INPUT_CANCEL));
    CHECK(hold(&s,0,0,0,15)&GY_INPUT_CANCEL);CHECK(s.input.direction==-1);
    hold(&s,0,-2000,0,42);s.now+=101;
    event=sample(&s,0,-2000,0);
    CHECK(!(event.events&GY_INPUT_CANCEL));
    CHECK(s.input.direction==GY_UP);
    hold(&s,0,0,0,8);hold(&s,0,-2000,0,42);
    CHECK(sample(&s,10000,0,1).events&GY_INPUT_CANCEL);
    CHECK(!(hold(&s,0,-2000,1,100)&GY_INPUT_CONFIRM));
    s=setup();s.now=UINT32_MAX-100;hold(&s,0,0,0,8);
    CHECK(hold(&s,0,-2000,0,45)&GY_INPUT_SELECT);CHECK(hold(&s,0,-2000,1,8)&GY_INPUT_CONFIRM);
    s=setup();hold(&s,0,0,0,8);hold(&s,0,-2000,0,42);s.available[GY_UP]=1;
    CHECK(sample(&s,0,-2000,1).events&GY_INPUT_CANCEL);
    CHECK(!(hold(&s,0,-2000,1,45)&GY_INPUT_CONFIRM));
    /* REQ-320-A: 0 soldiers but numbers can move -> still SELECT with count 0. */
    {
        GyGame g;GyPreview preview;unsigned legal=0;int ready=0;
        memset(&g,0,sizeof(g));g.config=gy_default_config();g.outcome=GY_PLAYING;
        g.cells[0][1].kind=GY_NUMBER;g.cells[0][1].value=2;
        g.cells[1][1].kind=GY_NUMBER;g.cells[1][1].value=2;
        s=setup();s.legal=0;
        for(d=0;d<4;++d){
            s.available[d]=gy_eligible_count(&g,d);
            if(gy_direction_legal(&g,d))legal|=1u<<d;
        }
        s.legal=legal;
        CHECK(legal&(1u<<GY_UP));
        hold(&s,0,0,0,8);
        hold(&s,0,-2000,0,45);
        event=sample(&s,0,-2000,0);
        CHECK((event.events&GY_INPUT_SELECT)||s.input.direction==GY_UP);
        CHECK(s.input.direction==GY_UP&&s.input.count==0);
        {GyMove m={s.input.direction,s.input.count,s.input.aim};ready=gy_preview(&g,m,&preview);}
        CHECK(ready);CHECK(gy_commit(&g,&preview));
        CHECK(g.cells[0][1].kind==GY_NUMBER&&g.cells[0][1].value==4);
    }
    /* REQ-320-B: input reports the lateral aim; direction still UP. */
    s=setup();hold(&s,0,0,0,8);
    hold(&s,-1000,-2000,0,45);
    CHECK(s.input.direction==GY_UP);
    CHECK(s.input.aim<=-40&&s.input.aim>=-60);
    event=sample(&s,-1000,-2000,0);
    CHECK(event.aim==s.input.aim);
    /* Feed the real input events to the real preview/commit interfaces. */
    {
        GyGame g,before;GyPreview preview;int ready=0;unsigned legal=0;
        gy_init(&g,gy_default_config(),20261015);before=g;s=setup();
        for(d=0;d<4;++d){
            s.available[d]=gy_eligible_count(&g,d);
            if(gy_direction_legal(&g,d))legal|=1u<<d;
        }
        s.legal=legal;
        hold(&s,0,0,0,8);
        for(i=0;i<60;++i){
            event=sample(&s,0,-2000,i>=48);
            if(event.events&GY_INPUT_SELECT){GyMove m={event.direction,event.count,event.aim};ready=gy_preview(&g,m,&preview);}
            if(event.events&GY_INPUT_CONFIRM){CHECK(ready);CHECK(gy_commit(&g,&preview));}
        }
        CHECK(g.revision==before.revision+1&&g.turn==1);
        CHECK(!(hold(&s,0,-2000,1,100)&GY_INPUT_CONFIRM));
    }
    /* 配置/会话/落点映射：补齐输入模块的纯逻辑覆盖。 */
    {
        GyInput cfg;
        uint32_t last=0;
        GyInputPlace place;
        GyGame g;

        gy_input_reset(&cfg);
        gy_input_configure(&cfg,-100,999);
        CHECK(cfg.dir_buffer_pct==GY_INPUT_BUFFER_MIN);
        CHECK(cfg.angle_buffer_pct==GY_INPUT_BUFFER_MAX);
        gy_input_configure(&cfg,25,35);
        CHECK(cfg.dir_buffer_pct==25&&cfg.angle_buffer_pct==35);

        last=0;cfg.direction=GY_UP;
        CHECK(gy_input_begin_session(&cfg,0,&last)==0);
        CHECK(gy_input_begin_session(&cfg,7,&last)==1);
        CHECK(last==7&&cfg.direction==-1);
        CHECK(gy_input_begin_session(&cfg,7,&last)==0);
        CHECK(gy_input_begin_session(NULL,7,&last)==0);
        CHECK(gy_input_begin_session(&cfg,8,NULL)==0);

        CHECK(gy_input_direction(0,0)==-1);
        CHECK(gy_input_aim(0,0,GY_UP)==0);
        CHECK(gy_input_aim(0,-1000,-1)==0);
        CHECK(gy_input_aim(500,-1000,GY_UP)==50);

        s=setup();hold(&s,0,-2000,0,45);
        event=gy_input_update(&s.input,0,-2000,0,s.now+5,NULL,0xF);
        CHECK(event.events&GY_INPUT_CANCEL);
        s=setup();hold(&s,0,-2000,0,45);
        event=gy_input_update(&s.input,10000,0,0,s.now+5,s.available,0xF);
        CHECK(event.events&GY_INPUT_CANCEL);

        /* aim-only 更新：同方向/数量、aim 变化 >=10 且稳定 200ms。 */
        s=setup();hold(&s,0,0,0,8);hold(&s,0,-2000,0,45);
        CHECK(s.input.direction==GY_UP&&s.input.aim==0);
        CHECK(hold(&s,1000,-2000,0,45)&GY_INPUT_UPDATE);
        CHECK(s.input.direction==GY_UP);
        CHECK(s.input.aim>0);

        memset(&g,0,sizeof(g));
        g.config=gy_default_config();g.outcome=GY_PLAYING;
        memset(&place,0,sizeof(place));
        CHECK(gy_input_place_cell(NULL,0,-1000,0,0,&place)==0);
        CHECK(gy_input_place_cell(&g,0,-1000,0,0,NULL)==0);
        /* 四向边：aim=0 落在中间列/行。 */
        CHECK(gy_input_place_cell(&g,0,-1000,0,0,&place)==1);
        CHECK(place.row==0&&place.column==3);
        CHECK(gy_input_place_cell(&g,0,1000,0,0,&place)==1);
        CHECK(place.row==5&&place.column==3);
        CHECK(gy_input_place_cell(&g,-1000,0,0,0,&place)==1);
        CHECK(place.row==3&&place.column==0);
        CHECK(gy_input_place_cell(&g,1000,0,0,0,&place)==1);
        CHECK(place.row==3&&place.column==5);
        /* 死区保持当前格；目标被占沿射线内缩；整列占满返回 0。 */
        CHECK(gy_input_place_cell(&g,0,0,2,2,&place)==1);
        CHECK(place.row==2&&place.column==2);
        g.cells[5][3].kind=GY_SOLDIER;
        CHECK(gy_input_place_cell(&g,0,1000,0,0,&place)==1);
        CHECK(place.row==4&&place.column==3);
        for(d=0;d<6;++d)g.cells[d][3].kind=GY_SOLDIER;
        CHECK(gy_input_place_cell(&g,0,1000,0,0,&place)==0);
    }
    printf("PASS: input %d assertions; TC-309 simulated angles/time only\n",checks);
    return 0;
}
