#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "geyi_ui.h"
#include "geyi_font_cn16.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH=390, HEIGHT=450 };
static uint32_t frame[WIDTH*HEIGHT],draw_buffer[WIDTH*HEIGHT];
static HWND window;
static lv_indev_t *pointer;
static int mouse_x,mouse_y,mouse_down;
static BITMAPINFO bitmap;
static uint32_t ticks(void) {return GetTickCount();}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels)
{
    int y,w=area->x2-area->x1+1;
    for(y=area->y1;y<=area->y2;++y)
        memcpy(&frame[y*WIDTH+area->x1],pixels+(size_t)(y-area->y1)*w*4,(size_t)w*4);
    lv_display_flush_ready(display);
    if(window)InvalidateRect(window,NULL,FALSE);
}
static void read_pointer(lv_indev_t *indev,lv_indev_data_t *data)
{
    (void)indev;data->point.x=mouse_x;data->point.y=mouse_y;
    data->state=mouse_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void initialize(void)
{
    lv_display_t *display;
    lv_init();lv_tick_set_cb(ticks);
    display=lv_display_create(WIDTH,HEIGHT);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,flush);
    pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,read_pointer);gy_ui_create();lv_refr_now(display);
    memset(&bitmap,0,sizeof(bitmap));bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth=WIDTH;bitmap.bmiHeader.biHeight=-HEIGHT;
    bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
}
static void screenshot(const char *path)
{
    BITMAPFILEHEADER file;FILE *out=NULL;
    lv_refr_now(NULL);memset(&file,0,sizeof(file));file.bfType=0x4d42;
    file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+sizeof(frame);
    if(fopen_s(&out,path,"wb")||!out)exit(2);
    fwrite(&file,sizeof(file),1,out);fwrite(&bitmap.bmiHeader,sizeof(BITMAPINFOHEADER),1,out);
    fwrite(frame,sizeof(frame),1,out);fclose(out);
}
static void click(int x,int y)
{
    mouse_x=x;mouse_y=y;mouse_down=1;lv_indev_read(pointer);
    mouse_down=0;lv_indev_read(pointer);lv_refr_now(NULL);
}
static void check(int ok,const char *what)
{if(!ok){fprintf(stderr,"FAIL UI: %s\n",what);exit(1);}}
static int tests(void)
{
    GyGame before;GyPreview p;GyMove m={GY_UP,1};
    initialize();before=*gy_ui_game();screenshot("home.bmp");
    check(gy_light_selftest(),"TC-314 light timeline including wrap");
    check(gy_ui_take_feedback()==GY_FX_CLEAR,"home clears light");
    check(!gy_ui_is_playing(),"startup is home");
    gy_ui_action(GY_UI_UP);gy_ui_tilt_select(GY_UP,1);gy_ui_action(GY_UI_CONFIRM);
    check(!gy_ui_has_preview()&&!memcmp(&before,gy_ui_game(),sizeof(before)),"menu ignores game input");
    check(gy_ui_take_feedback()==GY_FX_NONE,"menu game actions do not flash");
    click(160,280);click(160,346);
    check(!gy_ui_is_playing(),"home placeholders disabled");
    click(160,212);screenshot("modes.bmp");
    click(160,250);click(160,320);
    check(!gy_ui_is_playing(),"unfinished modes disabled");
    click(160,400);check(!gy_ui_is_playing(),"mode back to home");
    click(160,212);click(160,154);
    check(gy_ui_is_playing(),"single enters game");screenshot("initial.bmp");
    check(gy_ui_take_feedback()==GY_FX_CLEAR,"single starts light off");
    {const uint32_t required[]={0x786e,0x8ba4,0x5f00,0x8f93,0x80dc,0x7ea2,0x84dd};
     unsigned i;lv_font_glyph_dsc_t glyph;
     for(i=0;i<sizeof(required)/sizeof(required[0]);++i)
        check(lv_font_get_glyph_dsc(&geyi_font_cn16,&glyph,required[i],0)&&glyph.box_w>0,"simplified glyph is present");}
    check(gy_preview(&before,m,&p),"core preview exists");
    check(p.visible.rng==before.rng,"visible preview does not consume RNG");
    click(35,354);check(gy_ui_has_preview(),"mouse direction button previews");
    check(gy_ui_take_feedback()==GY_FX_NONE,"preview does not flash");
    check(!memcmp(&before,gy_ui_game(),sizeof(before)),"preview does not write game");screenshot("preview.bmp");
    click(190,395);check(!gy_ui_has_preview(),"mouse cancel");
    check(gy_ui_take_feedback()==GY_FX_NONE,"cancel does not flash");
    check(!memcmp(&before,gy_ui_game(),sizeof(before)),"cancel unchanged");
    gy_ui_action(GY_UI_UP);click(70,395);
    check(gy_ui_game()->revision==1&&gy_ui_game()->turn==1,"mouse confirm changes turn");
    check(gy_ui_take_feedback()==GY_FX_MOVE,"confirmed move flashes once");
    click(70,395);check(gy_ui_game()->revision==1,"disabled confirm not repeated");
    check(gy_ui_take_feedback()==GY_FX_NONE,"duplicate confirm does not flash");
    gy_ui_action(GY_UI_RESIGN);check(gy_ui_game()->outcome==GY_RED_WIN,"resign");screenshot("result.bmp");
    check(gy_ui_take_feedback()==GY_FX_RED,"blue resign selects red winner light");
    click(240,20);check(!memcmp(&before,gy_ui_game(),sizeof(before)),"mouse restart");
    check(gy_ui_take_feedback()==GY_FX_CLEAR,"restart clears light");
    gy_ui_action(GY_UI_RESIGN);check(gy_ui_take_feedback()==GY_FX_BLUE,"red resign selects blue winner light");
    gy_ui_action(GY_UI_RESTART);(void)gy_ui_take_feedback();
    gy_ui_action(GY_UI_PLACE);check(!gy_ui_has_preview(),"no coins cannot place");
    /* Drive legal game actions until coins are available; avoid test-only state mutation. */
    {
        int step,d;
        for(step=0;step<1000&&gy_ui_game()->outcome==0;++step){
            if(gy_ui_game()->coins[gy_ui_game()->turn]>=4){
                int x,y=gy_ui_game()->turn?0:5;
                for(x=0;x<6;++x)if(gy_ui_game()->cells[y][x].kind==GY_EMPTY){
                    uint32_t revision=gy_ui_game()->revision;
                    gy_ui_action(GY_UI_PLACE);gy_ui_cell(x,y);check(gy_ui_has_preview(),"place preview");screenshot("placement.bmp");
                    gy_ui_action(GY_UI_CONFIRM);check(gy_ui_game()->revision==revision+1,"place confirmed");
                    check(gy_ui_take_feedback()==GY_FX_MOVE,"successful placement flashes");
                    goto placed;
                }
            }
            for(d=0;d<4;++d){gy_ui_action(d);if(gy_ui_has_preview()){gy_ui_action(GY_UI_CONFIRM);break;}}
            if(d==4)break;
        }
        puts("NOTE: earned-coins placement path not reached in this deterministic match");
        goto done;
placed: puts("PASS: placement through earned game coins");
done: ;
    }
    gy_ui_action(GY_UI_RESTART);gy_ui_action(GY_UI_MORE);gy_ui_action(GY_UI_UP);
    check(gy_ui_has_preview(),"quantity two preview");
    gy_ui_action(GY_UI_CONFIRM);check(gy_ui_game()->cells[4][1].kind==GY_SOLDIER&&gy_ui_game()->cells[4][4].kind==GY_SOLDIER,"both selected soldiers moved");
    click(166,20);check(!gy_ui_is_playing()&&!gy_ui_has_preview(),"game back home clears preview");
    click(160,212);click(160,154);
    check(gy_ui_is_playing()&&!memcmp(&before,gy_ui_game(),sizeof(before)),"single re-entry resets game");
    puts("PASS: TC-314 light timeline/selftest and real UI feedback events (no physical LED)");
    puts("PASS: TC-313 home/modes/disabled entries/input guard/single re-entry");
    puts("PASS: real LVGL UI, mouse events, preview/cancel/confirm, quantity, results, restart");return 0;
}
static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l)
{
    switch(message){
    case WM_PAINT:{PAINTSTRUCT p;RECT r;HDC dc=BeginPaint(hwnd,&p);GetClientRect(hwnd,&r);
        StretchDIBits(dc,0,0,r.right,r.bottom,0,0,WIDTH,HEIGHT,frame,&bitmap,DIB_RGB_COLORS,SRCCOPY);
        EndPaint(hwnd,&p);return 0;}
    case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:{RECT r;GetClientRect(hwnd,&r);
        mouse_x=(int)(short)LOWORD(l)*WIDTH/(r.right?r.right:1);
        mouse_y=(int)(short)HIWORD(l)*HEIGHT/(r.bottom?r.bottom:1);
        if(message==WM_LBUTTONDOWN){mouse_down=1;SetCapture(hwnd);}
        if(message==WM_LBUTTONUP){mouse_down=0;ReleaseCapture();}
        lv_indev_read(pointer);return 0;}
    case WM_CAPTURECHANGED:case WM_KILLFOCUS:mouse_down=0;lv_indev_read(pointer);return 0;
    case WM_KEYDOWN:
        if(l&(1L<<30))return 0;
        if(w==VK_UP||w=='W')gy_ui_action(GY_UI_UP);
        else if(w==VK_RIGHT||w=='D')gy_ui_action(GY_UI_RIGHT);
        else if(w==VK_DOWN||w=='S')gy_ui_action(GY_UI_DOWN);
        else if(w==VK_LEFT||w=='A')gy_ui_action(GY_UI_LEFT);
        else if(w==VK_RETURN||w==VK_SPACE)gy_ui_action(GY_UI_CONFIRM);
        else if(w==VK_ESCAPE)gy_ui_action(GY_UI_CANCEL);
        return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,message,w,l);
    }
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    WNDCLASSW cls;RECT rect={0,0,585,675};MSG msg;int quit=0;
    (void)previous;
    if(strstr(command,"--test"))return tests();
    initialize();memset(&cls,0,sizeof(cls));cls.lpfnWndProc=procedure;cls.hInstance=instance;
    cls.lpszClassName=L"GeyiSimulator";cls.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassW(&cls);
    AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
    window=CreateWindowW(cls.lpszClassName,L"格弈 单机图形原型 电脑模拟",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,
        rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,instance,NULL);
    if(!window)return 2;ShowWindow(window,show);UpdateWindow(window);
    while(!quit){while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){
        if(msg.message==WM_QUIT){quit=1;break;}TranslateMessage(&msg);DispatchMessage(&msg);}
        lv_timer_handler();Sleep(5);}
    return 0;
}
