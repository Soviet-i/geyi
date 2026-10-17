#include "geyi_game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void draw(const GyGame *g)
{
    int x,y;
    printf("\nTurn=%s coins R:%d B:%d revision=%u outcome=%d\n",
           g->turn ? "BLUE" : "RED",g->coins[0],g->coins[1],g->revision,g->outcome);
    printf("       "); for(x=0;x<g->config.size;++x) printf("%7d",x); puts("");
    for(y=0;y<g->config.size;++y) {
        printf("row %d: ",y);
        for(x=0;x<g->config.size;++x) {
            const GyCell *c=&g->cells[y][x];
            if(c->kind==GY_SOLDIER) printf(" %c:%-4d",c->owner?'B':'R',c->hp);
            else if(c->kind==GY_NUMBER) printf(" %6u",c->value);
            else printf("      .");
        }
        puts("");
    }
    if(g->outcome) printf("RESULT: %s\n",g->outcome==GY_DRAW?"DRAW":g->outcome==GY_RED_WIN?"RED WIN":"BLUE WIN");
}
int main(void)
{
    GyGame g; GyPreview preview; char line[100],cmd[30]; int count,ready=0;
    const char *directions="wdsa";
    gy_init(&g,gy_default_config(),20261015);
    puts("GEYI single-board rules prototype (keyboard replaces tilt)");
    puts("w/d/s/a COUNT: preview up/right/down/left; 0 allowed only if no soldier eligible");
    puts("ok: confirm preview | cancel | place COLUMN | resign | restart | quit");
    puts("No hardware, AI or Bluetooth. R/B:health. Outcome 0=playing 1=red 2=blue 3=draw.");
    draw(&g);
    while(printf("> "),fgets(line,sizeof(line),stdin)) {
        char *token = line + strspn(line," \t\r\n");
        char *argument;
        size_t length = strcspn(token," \t\r\n");
        if (!length || length >= sizeof(cmd)) continue;
        memcpy(cmd,token,length);cmd[length]=0;
        argument = token + length;
        argument += strspn(argument," \t\r\n");
        if (!*argument) argument=NULL;
        count=1;
        if(argument) {
            char *end; long parsed = strtol(argument,&end,10);
            end += strspn(end," \t\r\n");
            if (*end || parsed < 0 || parsed > GY_MAX*GY_MAX) {puts("Invalid number");continue;}
            count=(int)parsed;
        }
        if(!strcmp(cmd,"place")&&!argument){puts("place requires COLUMN");continue;}
        if(!strcmp(cmd,"quit")) break;
        if(!strcmp(cmd,"restart")) {gy_init(&g,gy_default_config(),20261015);ready=0;}
        else if(!strcmp(cmd,"cancel")) ready=0;
        else if(!strcmp(cmd,"ok")) {puts(ready&&gy_commit(&g,&preview)?"COMMITTED":"REJECTED");ready=0;}
        else if(!strcmp(cmd,"place")) {puts(gy_place(&g,count)?"PLACED":"REJECTED");ready=0;}
        else if(!strcmp(cmd,"resign")) {gy_resign(&g);ready=0;}
        else if(strlen(cmd)==1 && strchr(directions,cmd[0])) {
            GyMove m; m.direction=(int)(strchr(directions,cmd[0])-directions);m.count=count;
            ready=gy_preview(&g,m,&preview);
            if(ready) {puts("PREVIEW ONLY (includes proposed next random tile):");draw(&preview.after);}
            else puts("INVALID: direction/count blocked. State unchanged.");
        } else puts("Unknown command");
        puts("CURRENT STATE:");draw(&g);
    }
    return 0;
}
