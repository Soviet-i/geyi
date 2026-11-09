#ifdef GEYI_WITH_LVGL
#include "geyi_board.h"
#include "geyi_ui.h"
#include "geyi_input.h"
#include "geyi_config.h"
#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct {int x,y,valid;uint32_t stamp,seq;} Motion;
static struct rt_mutex motion_lock;
static Motion shared;
static GyInput input;
static uint32_t last_revision;
static int previous_place=-1,previous_column=-1,previous_preview;
static uint32_t milliseconds(void)
{return (uint32_t)((uint64_t)rt_tick_get()*1000/RT_TICK_PER_SECOND);}
/* 按当前对局模式读取参数页里的方向/角度缓冲阈值。
 * 参数页只允许在非对局页修改；每局开始/换手重置输入时应用一次，
 * 因此局中参数不会改变当前对局的输入手感（REQ-407）。 */
static void reset_input(void)
{
    const GyParamSet *p=gy_param_mode(gy_ui_ai_mode()?GY_MODE_PVE:GY_MODE_SINGLE);
    gy_input_reset(&input);
    if(p)gy_input_configure(&input,p->dir_buffer,p->angle_buffer);
}
static int read_register(struct rt_i2c_bus_device *bus,uint8_t reg,void *data,uint16_t count)
{return rt_i2c_mem_read(bus,0x6a,reg,8,data,count)==count;}
static int write_register(struct rt_i2c_bus_device *bus,uint8_t reg,uint8_t value)
{
    uint8_t bytes[2]={reg,value};struct rt_i2c_msg msg={0};
    msg.addr=0x6a;msg.flags=RT_I2C_WR;msg.len=2;msg.buf=bytes;
    return rt_i2c_transfer(bus,&msg,1)==1;
}
static int16_t word(const uint8_t *p) {return (int16_t)(p[0]|(uint16_t)p[1]<<8);}
static void publish(Motion *m)
{rt_mutex_take(&motion_lock,RT_WAITING_FOREVER);shared=*m;rt_mutex_release(&motion_lock);}
static float wrap(float a)
{while(a>180)a-=360;while(a<-180)a+=360;return a;}
static void sample_thread(void *parameter)
{
    struct rt_i2c_bus_device *bus;uint8_t id=0,old_acc=0,old_ctrl=0,data[6],ready;
    int configured=0,stable=0,calibrated=0,missed=0;
    float zero_x=0,zero_y=0,previous_x=0,previous_y=0,filtered_x=0,filtered_y=0;
    Motion motion={0};(void)parameter;
    HAL_PIN_Set(PAD_PA40,I2C3_SCL,PIN_PULLUP,1);HAL_PIN_Set(PAD_PA39,I2C3_SDA,PIN_PULLUP,1);
    bus=rt_i2c_bus_device_find("i2c3");
    if(!bus||!read_register(bus,0x0f,&id,1)||id!=0x6a){
        rt_kprintf("[geyi:imu] FAIL bus/id=%02x\n",id);return;
    }
    rt_kprintf("[geyi:imu] WHO_AM_I=%02x keep still for calibration\n",id);
    if(!read_register(bus,0x10,&old_acc,1)||!read_register(bus,0x12,&old_ctrl,1))return;
    configured=1;
    if(!write_register(bus,0x12,(old_ctrl&~2)|0x44)||!write_register(bus,0x10,0x40))goto fail;
    for(;;){
        if(!read_register(bus,0x1e,&ready,1))goto fail;
        if(ready&1){
            float ax,ay,az,x,y,norm;
            if(!read_register(bus,0x28,data,6))goto fail;
            ax=-word(data+2)*0.000061f;ay=word(data)*0.000061f;az=word(data+4)*0.000061f;
            norm=sqrtf(ax*ax+ay*ay+az*az);
            if(norm<0.7f||norm>1.3f){motion.valid=0;publish(&motion);stable=0;rt_thread_mdelay(20);continue;}
            x=atan2f(ax,az)*57.29578f;y=atan2f(-ay,sqrtf(ax*ax+az*az))*57.29578f;
            if(!calibrated){
                if(stable&&fabsf(wrap(x-previous_x))<2&&fabsf(y-previous_y)<2){
                    zero_x=wrap(zero_x+wrap(x-zero_x)/(stable+1));zero_y+=(y-zero_y)/(stable+1);++stable;
                }else{stable=1;zero_x=x;zero_y=y;}
                previous_x=x;previous_y=y;
                if(stable>=32){calibrated=1;rt_kprintf("[geyi:imu] calibrated zero_x=%d zero_y=%d\n",(int)(zero_x*100),(int)(zero_y*100));}
            }else{
                filtered_x+=(wrap(x-zero_x)-filtered_x)*0.3f;
                filtered_y+=(y-zero_y-filtered_y)*0.3f;
                motion.x=(int)(filtered_x*100);motion.y=(int)(filtered_y*100);
                motion.valid=1;motion.stamp=milliseconds();++motion.seq;publish(&motion);
            }
            missed=0;
        }else if(++missed>10)goto fail;
        rt_thread_mdelay(20);
    }
fail:
    motion.valid=0;publish(&motion);rt_kprintf("[geyi:imu] FAIL sample/config; tilt disabled, touch still available\n");
    if(configured){write_register(bus,0x10,old_acc);write_register(bus,0x12,old_ctrl);}
}
typedef struct {int raw,stable,armed;uint32_t since;} Button;
/* Only KEY2 (PA43) is a game key. PA34 is the power/download key and must
 * not be configured, read, or debounced by the game input path. */
static Button key2_button;
static int pressed(Button *b,int raw,uint32_t now)
{
    raw=!!raw;if(raw!=b->raw){b->raw=raw;b->since=now;}
    if((uint32_t)(now-b->since)<20)return 0;
    if(!raw)b->armed=1;
    if(raw!=b->stable){b->stable=raw;if(raw&&b->armed){b->armed=0;return 1;}}
    return 0;
}
int gy_board_start(void)
{
    rt_thread_t thread;
    if(rt_mutex_init(&motion_lock,"gy_motion",RT_IPC_FLAG_PRIO)!=RT_EOK)return -1;
    /* KEY2 only. PA34 stays untouched: it is the power/download key. */
    HAL_PIN_Set(PAD_PA43,GPIO_A43,PIN_NOPULL,1);rt_pin_mode(43,PIN_MODE_INPUT);
    gy_input_reset(&input);
    thread=rt_thread_create("gy_imu",sample_thread,RT_NULL,4096,18,10);
    if(!thread)return -1;
    rt_thread_startup(thread);return 0;
}
void gy_board_poll(void)
{
    static uint32_t last_motion_log;
    Motion m;uint32_t now=milliseconds();GyInputEvent event;int available[4],d,placing,key2;
    const GyGame *game=gy_ui_game();
    if(!gy_ui_is_playing()){
        gy_input_reset(&input);previous_column=-1;previous_place=0;previous_preview=0;
        return;
    }
    rt_mutex_take(&motion_lock,RT_WAITING_FOREVER);m=shared;rt_mutex_release(&motion_lock);
    key2=pressed(&key2_button,rt_pin_read(43),now);
    if(m.valid&&(uint32_t)(now-last_motion_log)>=1000){
        last_motion_log=now;
        rt_kprintf("[geyi:motion] x=%d y=%d key2=%d revision=%u\n",m.x,m.y,key2,game->revision);
    }
    /* KEY2 is the only game key; PA34 (KEY1/power) is never read.
     * Semantics live in gy_ui_key2() so the desktop test can exercise the
     * same decision without RT-Thread. */
    if(key2){
        int was_placing=gy_ui_is_placing();
        gy_ui_key2();
        rt_kprintf("[geyi:key] KEY2 was_placing=%d now_placing=%d revision=%u turn=%d\n",
                   was_placing,gy_ui_is_placing(),gy_ui_game()->revision,gy_ui_game()->turn);
        reset_input();previous_column=-1;
        last_revision=gy_ui_game()->revision;
        previous_place=gy_ui_is_placing();
        return;
    }
    placing=gy_ui_is_placing();
    /* 预览消失（确认/取消/无效确认清空）时重置状态机；否则输入层仍以为
     * 同一方向/数量“可见”，再次倾斜不会发 SELECT/UPDATE，UI 不再出现预览。 */
    {
        int preview=gy_ui_has_preview();
        if(game->revision!=last_revision||placing!=previous_place||
           (previous_preview&&!preview)){
            reset_input();last_revision=game->revision;previous_place=placing;
            previous_column=-1;
        }
        previous_preview=preview;
    }
    if(!m.valid||(uint32_t)(now-m.stamp)>100||abs(m.x)>9000||abs(m.y)>9000){
        if(!placing)gy_ui_tilt_cancel();
        reset_input();return;
    }
    if(placing){
        int size=game->config.size,col;
        if(size<GY_SIZE_MIN||size>GY_SIZE_MAX)size=6;
        /* 进入放兵后先不预选列：KEY2 再按一次应退出；水平倾斜越过 4° 才选列。 */
        if(previous_column<0&&abs(m.x)<GY_INPUT_PLACE_DEAD)return;
        /* 离倾斜方向最近的己方底线空列；死区/半列回差在 gy_input_place_column 内。 */
        col=gy_input_place_column(game,game->turn,m.x,m.y,previous_column);
        if(col>=0&&col<size&&col!=previous_column){
            previous_column=col;
            gy_ui_cell(col,game->turn?0:size-1);
        }
        return;
    }
    /* 按钮预览期间忽略倾斜；重置状态机，避免按钮预览结束后补发旧选择。 */
    if(!gy_ui_tilt_allowed()){reset_input();return;}
    for(d=0;d<4;++d)available[d]=gy_eligible_count(game,d);
    /* Pass key=0: the tilt state machine must never consume PA34 (KEY1). */
    event=gy_input_update(&input,m.x,m.y,0,now,available);
    if(event.events&GY_INPUT_CANCEL)gy_ui_tilt_cancel();
    if(event.events&(GY_INPUT_SELECT|GY_INPUT_UPDATE)){
        gy_ui_tilt_select(event.direction,event.count);
        rt_kprintf("[geyi:tilt] x=%d y=%d dir=%d count=%d preview=%d event=%d\n",
                   m.x,m.y,event.direction,event.count,gy_ui_has_preview(),event.events);
    }
    /* GY_INPUT_CONFIRM is intentionally unused on board: KEY1 is not a game key. */
}
#endif
