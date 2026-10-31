#include "geyi_game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL line %d: %s\n",__LINE__,#x); return 1;} } while(0)
static GyGame empty(void)
{
    GyGame g; gy_init(&g,gy_default_config(),7);
    memset(g.cells,0,sizeof(g.cells));
    return g;
}
static void soldier(GyGame *g,int x,int y,int owner,int hp)
{ GyCell c={GY_SOLDIER,owner,hp,0};g->cells[y][x]=c; }
static void number(GyGame *g,int x,int y,uint32_t value)
{ GyCell c={GY_NUMBER,0,0,value};g->cells[y][x]=c; }
static int act(GyGame *g,int d,int count)
{ GyPreview p;GyMove m={d,count};return gy_preview(g,m,&p)&&gy_commit(g,&p); }
static int count_kind(const GyGame *g,int kind)
{ int n=0,x,y;for(y=0;y<g->config.size;++y)for(x=0;x<g->config.size;++x)n+=g->cells[y][x].kind==kind;return n; }
int main(void)
{
    GyGame g,h; GyPreview p;GyMove m={GY_UP,1}; int d,i,k;
    CHECK(gy_selftest());
    CHECK(gy_init(&g,gy_default_config(),123));
    CHECK(gy_init(&h,gy_default_config(),123));CHECK(!memcmp(&g,&h,sizeof(g)));
    CHECK(count_kind(&g,GY_SOLDIER)==4);CHECK(count_kind(&g,GY_NUMBER)==2);
    {GyConfig c=gy_default_config();c.size=9;CHECK(!gy_init(&g,c,1));}
    gy_init(&g,gy_default_config(),123);h=g;
    CHECK(gy_preview(&g,m,&p));CHECK(!memcmp(&g,&h,sizeof(g)));
    CHECK(gy_commit(&g,&p));CHECK(g.revision==1);CHECK(!gy_commit(&g,&p));
    /* TC-303-05: a number behind the moving soldier follows into its old cell. */
    g=empty();soldier(&g,3,1,0,8);number(&g,1,1,2);soldier(&g,5,5,1,8);
    CHECK(gy_eligible_count(&g,GY_RIGHT)==1);
    {GyPreview q;GyMove mv={GY_RIGHT,1};
     CHECK(gy_preview(&g,mv,&q));
     CHECK(q.visible.cells[1][2].kind==GY_EMPTY);
     CHECK(q.visible.cells[1][3].kind==GY_NUMBER&&q.visible.cells[1][3].value==2);
     CHECK(q.visible.cells[1][4].kind==GY_SOLDIER&&q.visible.cells[1][4].owner==0);
     CHECK(q.visible.cells[1][4].hp==6);
     CHECK(gy_commit(&g,&q));CHECK(g.turn==1);}
    /* TC-304-05: the soldier blocked by a number is counted and moves after it. */
    g=empty();g.turn=1;soldier(&g,1,1,1,8);soldier(&g,4,1,1,8);
    number(&g,0,1,2);number(&g,2,1,2);soldier(&g,5,5,0,8);
    CHECK(gy_eligible_count(&g,GY_RIGHT)==2);
    {GyPreview q;GyMove mv={GY_RIGHT,2};
     CHECK(gy_preview(&g,mv,&q));
     CHECK(q.visible.cells[1][0].kind==GY_EMPTY);
     CHECK(q.visible.cells[1][1].kind==GY_NUMBER&&q.visible.cells[1][1].value==2);
     CHECK(q.visible.cells[1][2].kind==GY_SOLDIER&&q.visible.cells[1][2].owner==1&&q.visible.cells[1][2].hp==6);
     CHECK(q.visible.cells[1][3].kind==GY_EMPTY);
     CHECK(q.visible.cells[1][4].kind==GY_NUMBER&&q.visible.cells[1][4].value==2);
     CHECK(q.visible.cells[1][5].kind==GY_SOLDIER&&q.visible.cells[1][5].owner==1&&q.visible.cells[1][5].hp==6);
     CHECK(gy_commit(&g,&q));CHECK(g.outcome==GY_PLAYING);}
    /* TC-303-06: a soldier moves into the vacated cell but does not also attack. */
    g=empty();g.turn=1;soldier(&g,1,1,1,8);number(&g,2,1,2);
    soldier(&g,4,1,0,8);soldier(&g,5,5,0,8);
    CHECK(gy_eligible_count(&g,GY_RIGHT)==1);
    {GyPreview q;GyMove mv={GY_RIGHT,1};
     CHECK(gy_preview(&g,mv,&q));
     CHECK(q.visible.cells[1][1].kind==GY_EMPTY);
     CHECK(q.visible.cells[1][2].kind==GY_SOLDIER&&q.visible.cells[1][2].owner==1);
     CHECK(q.visible.cells[1][3].kind==GY_NUMBER&&q.visible.cells[1][3].value==2);
     CHECK(q.visible.cells[1][4].kind==GY_SOLDIER&&q.visible.cells[1][4].owner==0&&q.visible.cells[1][4].hp==6);}
    /* Numeric-only fixtures allow count zero when no soldier can move. */
    g=empty();number(&g,0,2,2);number(&g,1,2,2);number(&g,2,2,4);
    CHECK(act(&g,GY_LEFT,0));CHECK(g.cells[2][0].value==4&&g.cells[2][1].value==4);
    CHECK(g.coins[0]==1);
    g=empty();for(i=0;i<4;++i)number(&g,i,2,2);
    CHECK(act(&g,GY_LEFT,0));CHECK(g.cells[2][0].value==4&&g.cells[2][1].value==4);CHECK(g.coins[0]==2);
    for(d=0;d<4;++d) {
        int x=d%2?2:3,y=d%2?3:2;
        g=empty();number(&g,x,y,2);number(&g,x+(d%2?1:0),y+(d%2?0:1),2);
        CHECK(act(&g,d,0));CHECK(count_kind(&g,GY_NUMBER)==1);
    }
    for(d=0;d<4;++d) {
        int vx=(d==GY_RIGHT)-(d==GY_LEFT),vy=(d==GY_DOWN)-(d==GY_UP);
        g=empty();soldier(&g,2,2,0,8);soldier(&g,5,5,1,8);
        CHECK(act(&g,d,1));CHECK(g.cells[2+vy][2+vx].kind==GY_SOLDIER);
        CHECK(g.turn==1&&g.moves==1&&count_kind(&g,GY_NUMBER)==1);
    }
    g=empty();soldier(&g,0,2,0,8);soldier(&g,1,2,0,8);soldier(&g,5,5,1,8);
    CHECK(act(&g,GY_RIGHT,2));CHECK(g.cells[2][1].kind==GY_SOLDIER&&g.cells[2][2].kind==GY_SOLDIER);
    g=empty();soldier(&g,1,2,0,8);soldier(&g,2,2,1,3);
    CHECK(act(&g,GY_RIGHT,1));CHECK(g.cells[2][2].hp==1);CHECK(g.cells[2][1].hp==8);
    g.turn=0;CHECK(act(&g,GY_RIGHT,1));CHECK(g.outcome==GY_RED_WIN);CHECK(g.cells[2][1].kind==GY_SOLDIER);
    CHECK(g.coins[0]==2);CHECK(!act(&g,GY_UP,1));CHECK(!gy_place(&g,0));
    g=empty();number(&g,0,2,4);soldier(&g,2,2,1,3);soldier(&g,5,5,0,8);
    CHECK(act(&g,GY_RIGHT,0));CHECK(g.cells[2][1].value==4&&g.cells[2][2].kind==GY_EMPTY);CHECK(g.outcome==GY_RED_WIN);
    g=empty();number(&g,0,2,4);soldier(&g,2,2,0,6);soldier(&g,5,5,1,8);
    CHECK(act(&g,GY_RIGHT,1));CHECK(g.cells[2][3].hp==2);CHECK(g.coins[0]==0);
    /* Simultaneous last-soldier deaths must produce a draw. */
    g=empty();number(&g,0,1,4);soldier(&g,2,1,0,3);number(&g,0,3,4);soldier(&g,2,3,1,3);
    CHECK(act(&g,GY_RIGHT,1));CHECK(g.outcome==GY_DRAW);
    g=empty();soldier(&g,1,5,0,8);soldier(&g,1,0,1,8);g.coins[0]=4;
    h=g;CHECK(!gy_place(&g,1));CHECK(!memcmp(&g,&h,sizeof(g)));
    CHECK(!gy_place(&g,-1));CHECK(!gy_place(&g,6));CHECK(gy_place(&g,2));
    CHECK(g.coins[0]==0&&g.turn==1&&g.moves==0&&count_kind(&g,GY_NUMBER)==0);
    h=g;CHECK(!gy_place(&g,3));CHECK(!memcmp(&g,&h,sizeof(g)));
    CHECK(gy_resign(&g));CHECK(g.outcome==GY_RED_WIN);CHECK(!gy_resign(&g));
    g=empty();soldier(&g,0,0,0,8);soldier(&g,5,5,1,8);h=g;
    CHECK(!act(&g,GY_UP,1));CHECK(!memcmp(&g,&h,sizeof(g)));
    m.direction=99;CHECK(!gy_preview(&g,m,&p));CHECK(!memcmp(&g,&h,sizeof(g)));
    /* Equidistant eligible soldiers: row/column tie breaks consistently. */
    g=empty();soldier(&g,1,2,0,8);soldier(&g,4,2,0,8);soldier(&g,5,5,1,8);
    CHECK(act(&g,GY_UP,1));CHECK(g.cells[1][1].kind==GY_SOLDIER&&g.cells[2][4].kind==GY_SOLDIER);
    for(k=0;k<30;++k) {
        GyGame replay;
        gy_init(&g,gy_default_config(),(uint32_t)(k+1));replay=g;
        for(i=0;i<200&&g.outcome==GY_PLAYING;++i) {
            int accepted=0;
            for(d=0;d<4&&!accepted;++d) {
                int n;
                for(n=0;n<=4&&!accepted;++n) {
                    GyMove action={d,n};
                    if(gy_preview(&g,action,&p)) {CHECK(gy_commit(&g,&p));CHECK(act(&replay,d,n));accepted=1;}
                }
            }
            CHECK(!memcmp(&g,&replay,sizeof(g)));
            for(d=0;d<GY_MAX*GY_MAX;++d) {
                GyCell *c=&g.cells[d/GY_MAX][d%GY_MAX];
                CHECK(c->kind>=GY_EMPTY&&c->kind<=GY_NUMBER);
                if(c->kind==GY_SOLDIER)CHECK(c->hp>0&&(c->owner==0||c->owner==1));
                if(c->kind==GY_NUMBER)CHECK(c->value>=2&&!(c->value&(c->value-1)));
            }
            if(!accepted)break;
        }
    }
    printf("PASS: %d assertions; TC-301..307; TC-308 host selftest only\n",checks);
    return 0;
}
