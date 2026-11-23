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
/* 串口自动化测试的单板输入源。开启后只替换 IMU 读数与 KEY2 电平，
 * 后续的新鲜度判断、失效取消、按键防抖、倾斜状态机全部保持原样。 */
static int sim_active,sim_x,sim_y;
static uint32_t sim_key_until,sim_seq_no;
static uint32_t last_revision,last_session;
static uint32_t last_motion_seq,last_motion_at;
static int previous_place=-1,previous_row=-1,previous_column=-1,previous_preview;
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
static void sample_thread(void *parameter)
{
    struct rt_i2c_bus_device *bus;uint8_t id=0,old_acc=0,old_ctrl=0,data[6],ready;
    int configured=0,missed=0;
    float filtered_x=0,filtered_y=0;
    Motion motion={0};(void)parameter;
    HAL_PIN_Set(PAD_PA40,I2C3_SCL,PIN_PULLUP,1);HAL_PIN_Set(PAD_PA39,I2C3_SDA,PIN_PULLUP,1);
    bus=rt_i2c_bus_device_find("i2c3");
    if(!bus||!read_register(bus,0x0f,&id,1)||id!=0x6a){
        rt_kprintf("[geyi:imu] FAIL bus/id=%02x\n",id);return;
    }
    /* 平放零点 = 重力方向。LSM6DS3TR-C 在这块板上 Z 轴朝下：
     * 屏幕朝上平放时 az≈-1g，所以用 atan2(ax,-az) 让平放为 0；
     * 这是固定安装方向，不采样开机姿态。开机时板子斜着，读数仍是真实倾斜。 */
    rt_kprintf("[geyi:imu] WHO_AM_I=%02x flat=gravity (Z inverted mount, no boot sampling)\n",id);
    if(!read_register(bus,0x10,&old_acc,1)||!read_register(bus,0x12,&old_ctrl,1))return;
    configured=1;
    if(!write_register(bus,0x12,(old_ctrl&~2)|0x44)||!write_register(bus,0x10,0x40))goto fail;
    for(;;){
        if(!read_register(bus,0x1e,&ready,1))goto fail;
        if(ready&1){
            float ax,ay,az,x,y,norm;
            if(!read_register(bus,0x28,data,6))goto fail;
            /* X 轴按屏幕方向取正：真机右倾 x>0；LSM6DS3TR-C 的 X 轴与屏幕相反，
             * 原先取负会导致左右反。y 轴不变。 */
            ax=word(data+2)*0.000061f;ay=word(data)*0.000061f;az=word(data+4)*0.000061f;
            norm=sqrtf(ax*ax+ay*ay+az*az);
            if(norm<0.7f||norm>1.3f){motion.valid=0;publish(&motion);rt_thread_mdelay(20);continue;}
            x=atan2f(ax,-az)*57.29578f;y=atan2f(-ay,sqrtf(ax*ax+az*az))*57.29578f;
            filtered_x+=(x-filtered_x)*0.3f;
            filtered_y+=(y-filtered_y)*0.3f;
            motion.x=(int)(filtered_x*100);motion.y=(int)(filtered_y*100);
            motion.valid=1;motion.stamp=milliseconds();++motion.seq;publish(&motion);
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
void gy_board_test_tilt(int x,int y)
{sim_active=1;sim_x=x;sim_y=y;}
void gy_board_test_key(void)
{sim_key_until=milliseconds()+150;}
void gy_board_test_off(void)
{sim_active=0;sim_key_until=0;}
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
    static uint32_t last_cancel_log;
    Motion m;uint32_t now=milliseconds();GyInputEvent event;int available[4],d,placing,key2;
    unsigned legal;GyInputPlace target;int motion_bad,stale;
    const GyGame *game=gy_ui_game();
    if(!gy_ui_is_playing()){
        gy_input_reset(&input);previous_row=-1;previous_column=-1;previous_place=0;previous_preview=0;
        return;
    }
    if(sim_active){
        m.x=sim_x;m.y=sim_y;m.valid=1;m.stamp=now;m.seq=++sim_seq_no;
    }else{
        rt_mutex_take(&motion_lock,RT_WAITING_FOREVER);m=shared;rt_mutex_release(&motion_lock);
    }
    motion_bad=!m.valid||abs(m.x)>9000||abs(m.y)>9000;
    /* 只有“有效且在范围内”的新样本才刷新最后有效时间；超范围样本不能
     * 无限刷新新鲜度，否则持续 >90° 时永远不会触发失效取消。 */
    if(m.seq!=last_motion_seq&&!motion_bad){last_motion_seq=m.seq;last_motion_at=now;}
    key2=pressed(&key2_button,
                 sim_active?((int32_t)(sim_key_until-now)>0):rt_pin_read(43),now);
    placing=gy_ui_is_placing();
    if(gy_input_begin_session(&input,gy_ui_session(),&last_session)){
        reset_input();last_revision=game->revision;
        previous_row=previous_column=-1;previous_place=placing;previous_preview=0;
    }
    /* 预览消失（确认/取消/无效确认清空）时重置状态机；否则输入层仍以为
     * 同一方向/数量“可见”，再次倾斜不会发 SELECT/UPDATE，UI 不再出现预览。 */
    {
        int preview=gy_ui_has_preview();
        if(game->revision!=last_revision||placing!=previous_place||
           (previous_preview&&!preview)){
            reset_input();last_revision=game->revision;previous_place=placing;
            previous_row=previous_column=-1;
        }
    }
    stale=(uint32_t)(now-last_motion_at)>100;
    if(stale){
        /* 持续失效：先取消/清状态，且不处理 KEY2，避免确认冻结的旧预览。 */
        if((uint32_t)(now-last_cancel_log)>=1000){
            last_cancel_log=now;
            rt_kprintf("[geyi:tilt] cancel %s age=%u valid=%d\n",
                       motion_bad?"invalid":"stale",(unsigned)(now-last_motion_at),m.valid);
        }
        gy_ui_tilt_cancel();reset_input();
        previous_preview=gy_ui_has_preview();
        return;
    }
    /* KEY2 在新鲜度检查之后处理；瞬时 invalid（按键震动/坏读）仍允许确认。 */
    if(key2){
        int was_placing=gy_ui_is_placing();
        gy_ui_key2();
        rt_kprintf("[geyi:key] KEY2 was_placing=%d now_placing=%d revision=%u turn=%d\n",
                   was_placing,gy_ui_is_placing(),gy_ui_game()->revision,gy_ui_game()->turn);
        reset_input();previous_row=-1;previous_column=-1;last_revision=gy_ui_game()->revision;last_session=gy_ui_session();
        previous_place=gy_ui_is_placing();previous_preview=gy_ui_has_preview();
        return;
    }
    if(motion_bad){
        /* 单帧 IMU 无效（按键/轻微震动/一次坏读）只跳过本样本，不能清预览。 */
        previous_preview=gy_ui_has_preview();
        return;
    }
    if(placing){
        int size=game->config.size;
        if(size<GY_SIZE_MIN||size>GY_SIZE_MAX)size=6;
        /* 进入放兵后先不预选：KEY2 再按一次应退出；倾斜越过死区才选格。 */
        if(previous_column<0&&abs(m.x)<GY_INPUT_PLACE_DEAD&&abs(m.y)<GY_INPUT_PLACE_DEAD){
            previous_preview=gy_ui_has_preview();return;
        }
        /* 外围边/角离倾斜方位最近的空格；目标被占时沿射线内缩。 */
        if(!gy_input_place_cell(game,m.x,m.y,previous_row,previous_column,&target)){
            previous_preview=gy_ui_has_preview();return;
        }
        if(target.row!=previous_row||target.column!=previous_column){
            previous_row=target.row;previous_column=target.column;
            rt_kprintf("[geyi:place] x=%d y=%d row=%d col=%d\n",
                       m.x,m.y,target.row,target.column);
            gy_ui_cell(target.column,target.row);
        }
        previous_preview=gy_ui_has_preview();
        return;
    }
    /* 按钮预览期间忽略倾斜；重置状态机，避免按钮预览结束后补发旧选择。 */
    if(!gy_ui_tilt_allowed()){
        reset_input();previous_preview=gy_ui_has_preview();return;
    }
    legal=0;
    for(d=0;d<4;++d){
        available[d]=gy_eligible_count(game,d);
        if(gy_direction_legal(game,d))legal|=1u<<d;
    }
    /* Pass key=0: the tilt state machine must never consume PA34 (KEY1). */
    /* 输入状态机用 IMU 样本时间，而不是主循环轮询时间：触摸/重绘让主线程
     * 停顿 >100ms 时，m.stamp 仍是最新样本，不能误判为输入中断。 */
    event=gy_input_update(&input,m.x,m.y,0,m.stamp,available,legal);
    if(event.events&GY_INPUT_CANCEL){
        rt_kprintf("[geyi:tilt] input cancel dir=%d count=%d aim=%d\n",
                   event.direction,event.count,event.aim);
        gy_ui_tilt_cancel();
    }
    if(event.events&(GY_INPUT_SELECT|GY_INPUT_UPDATE)){
        gy_ui_tilt_select(event.direction,event.count,event.aim);
        rt_kprintf("[geyi:tilt] x=%d y=%d dir=%d count=%d aim=%d preview=%d event=%d\n",
                   m.x,m.y,event.direction,event.count,event.aim,gy_ui_has_preview(),event.events);
    }
    /* 采样“预览是否可见”必须在本帧倾斜喂入之后，否则屏幕确认清预览
     * 可能被上一帧的 previous_preview 漏掉。 */
    previous_preview=gy_ui_has_preview();
    /* GY_INPUT_CONFIRM is intentionally unused on board: KEY1 is not a game key. */
}
#endif
