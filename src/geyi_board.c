#ifdef GEYI_WITH_LVGL
#include "geyi_board.h"
#include "geyi_ui.h"
#include "geyi_input.h"
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
static int previous_place=-1,previous_column=-1;
static uint32_t milliseconds(void)
{return (uint32_t)((uint64_t)rt_tick_get()*1000/RT_TICK_PER_SECOND);}
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
static Button confirm_key,cancel_key;
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
    /* KEY1 is a power/download multiplexed input: preserve BSP pin settings. */
    HAL_PIN_Set(PAD_PA43,GPIO_A43,PIN_NOPULL,1);rt_pin_mode(43,PIN_MODE_INPUT);
    gy_input_reset(&input);
    thread=rt_thread_create("gy_imu",sample_thread,RT_NULL,4096,18,10);
    if(!thread)return -1;
    rt_thread_startup(thread);return 0;
}
void gy_board_poll(void)
{
    static uint32_t last_motion_log;
    Motion m;uint32_t now=milliseconds();GyInputEvent event;int available[4],d,placing;
    int key=rt_pin_read(34),confirm=pressed(&confirm_key,key,now),cancel=pressed(&cancel_key,rt_pin_read(43),now);
    const GyGame *game=gy_ui_game();
    rt_mutex_take(&motion_lock,RT_WAITING_FOREVER);m=shared;rt_mutex_release(&motion_lock);
    if(m.valid&&(uint32_t)(now-last_motion_log)>=1000){
        last_motion_log=now;
        rt_kprintf("[geyi:motion] x=%d y=%d key1=%d revision=%u\n",m.x,m.y,key,game->revision);
    }
    if(cancel){
        gy_ui_action(gy_ui_has_preview()||gy_ui_is_placing()?GY_UI_CANCEL:GY_UI_PLACE);
        gy_input_reset(&input);previous_column=-1;
        rt_kprintf("[geyi:key] KEY2 placing=%d\n",gy_ui_is_placing());
    }
    placing=gy_ui_is_placing();
    if(game->revision!=last_revision||placing!=previous_place){
        gy_input_reset(&input);last_revision=game->revision;previous_place=placing;previous_column=-1;
    }
    if(!m.valid||(uint32_t)(now-m.stamp)>100||abs(m.x)>9000||abs(m.y)>9000){
        if(input.direction>=0&&gy_ui_has_preview()&&!placing)gy_ui_action(GY_UI_CANCEL);
        gy_input_reset(&input);return;
    }
    if(placing){
        if(abs(m.x)>1000){int col=(m.x+3000)*6/6001;if(col<0)col=0;if(col>5)col=5;
            if(col!=previous_column){previous_column=col;gy_ui_cell(col,game->turn?0:5);}}
        if(confirm){gy_ui_action(GY_UI_CONFIRM);rt_kprintf("[geyi:key] place confirm revision=%u\n",gy_ui_game()->revision);}
        return;
    }
    for(d=0;d<4;++d)available[d]=gy_eligible_count(game,d);
    event=gy_input_update(&input,m.x,m.y,key,now,available);
    if(event.events&GY_INPUT_CANCEL)gy_ui_action(GY_UI_CANCEL);
    if(event.events&GY_INPUT_SELECT){
        gy_ui_tilt_select(event.direction,event.count);
        rt_kprintf("[geyi:tilt] x=%d y=%d dir=%d count=%d preview=%d\n",m.x,m.y,event.direction,event.count,gy_ui_has_preview());
    }
    if(event.events&GY_INPUT_CONFIRM){gy_ui_action(GY_UI_CONFIRM);rt_kprintf("[geyi:key] confirm revision=%u turn=%d\n",gy_ui_game()->revision,gy_ui_game()->turn);}
}
#endif
