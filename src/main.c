/*
 * course_starter 内置例程：串口打印 + 黄山派板载 RGB 灯颜色闪烁。
 *
 * 基于 SiFli-SDK 官方例程（Apache-2.0）：
 *   example/get-started/hello_world/rtt   —— 启动打印
 *   example/rt_device/rgb_led             —— RGB 灯驱动用法（SK6812MINI-HS，PA32，PWMT2 通道1）
 * 板型：sf32lb52-lchspi-ulp（立创·黄山派）。
 *
 * 行为：启动后打印 "Hello world!"，随后 RGB 灯按 红→绿→蓝 依次亮 500ms、灭 500ms，
 *       每个颜色串口打印一行日志。可作为 T0 的编译、烧录和串口取证对象。
 *
 * 新增 .c 文件直接放进 src/ 即可，src/SConscript 会自动收集。
 */
#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "geyi_game.h"
#include "geyi_input.h"
#include "geyi_feedback.h"
#include "geyi_ai.h"
#include "geyi_config.h"
#include "geyi_param_ui.h"
#include "drivers/rt_drv_pwm.h"
#ifdef GEYI_WITH_LVGL
#include "geyi_ui.h"
#include "geyi_board.h"
#include "littlevgl2rtt.h"
#endif

#define APP_NAME        "course_starter"
#define APP_VERSION     "0.1.0"

#define RGBLED_NAME     "rgbled"
#define BLINK_ON_MS     500
#define BLINK_OFF_MS    500

struct rgb_color
{
    const char *name;
    uint32_t    value;      /* 0xRRGGBB */
};

static const struct rgb_color color_table[] =
{
    {"red",   0xff0000},
    {"green", 0x00ff00},
    {"blue",  0x0000ff},
};

static struct rt_device *rgbled_device;

static int rgb_led_init(void)
{
#ifdef SF32LB52X
    HAL_PIN_Set(PAD_PA32, GPTIM2_CH1, PIN_NOPULL, 1);   /* pwmt2_cc1 */
    /* RGB 灯供电 */
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO3_3V3, true, true);
#endif
    rgbled_device = rt_device_find(RGBLED_NAME);
    if (!rgbled_device)
    {
        rt_kprintf("[%s] ERROR: device '%s' not found, check proj.conf RGB options\n", APP_NAME, RGBLED_NAME);
        return 0;
    }
    return 1;
}

static int rgb_led_set_color(uint32_t color)
{
    struct rt_rgbled_configuration configuration={0};
    configuration.color_rgb = color;
    return rt_device_control(rgbled_device, PWM_CMD_SET_COLOR, &configuration)==RT_EOK;
}

int main(void)
{
    rt_kprintf("\n[%s] v%s boot ok\n", APP_NAME, APP_VERSION);
    rt_kprintf("Hello world!\n");
    rt_kprintf("[geyi:selftest] core=%s (not a hardware game test)\n",
               gy_selftest() ? "PASS" : "FAIL");
    rt_kprintf("[geyi:selftest] input=%s (simulated angles only)\n",
               gy_input_selftest() ? "PASS" : "FAIL");
    rt_kprintf("[geyi:selftest] ai=%s\n", gy_ai_selftest() ? "PASS" : "FAIL");
    rt_kprintf("[geyi:selftest] param=%s\n", gy_param_selftest() ? "PASS" : "FAIL");
    rt_kprintf("[geyi:selftest] paramui=%s\n", gy_param_ui_selftest() ? "PASS" : "FAIL");

#ifdef GEYI_WITH_LVGL
    GyLight light={0};uint32_t last_light=UINT32_MAX;
    int light_ok=rgb_led_init();
    rt_kprintf("[geyi:selftest] light=%s\n",gy_light_selftest()?"PASS":"FAIL");
    if (littlevgl2rtt_init("lcd") != RT_EOK) {
        rt_kprintf("[geyi:lcd] FAIL initialize\n");
        return -1;
    }
    gy_ui_create();
    rt_kprintf("[geyi:lcd] READY %d x %d\n",(int)lv_display_get_horizontal_resolution(NULL),
               (int)lv_display_get_vertical_resolution(NULL));
    int board_ready=gy_board_start()==0;
    if(!board_ready)rt_kprintf("[geyi:board] FAIL worker start, touch remains available\n");
    rt_kprintf("[geyi:board] Keep board still ~1 second. KEY1 short=confirm KEY2=cancel/place\n");
    while(1){
        if(board_ready)gy_board_poll();
        lv_timer_handler();
        gy_ui_tick();
        {uint32_t now=(uint32_t)((uint64_t)rt_tick_get()*1000/RT_TICK_PER_SECOND);
        int event=gy_ui_take_feedback();uint32_t current;
        if(event){gy_light_start(&light,event,now);rt_kprintf("[geyi:light] event=%d\n",event);}
        current=gy_light_color(&light,now);
        if(light_ok&&current!=last_light){
            if(!rgb_led_set_color(current)){light_ok=0;rt_kprintf("[geyi:light] FAIL write; game continues\n");}
            last_light=current;
        }}
        rt_thread_mdelay(5);
    }
#else
    if(!rgb_led_init())return -1;

    while (1)
    {
        for (int i = 0; i < (int)(sizeof(color_table) / sizeof(color_table[0])); i++)
        {
            rt_kprintf("[%s] rgb -> %s\n", APP_NAME, color_table[i].name);
            rgb_led_set_color(color_table[i].value);
            rt_thread_mdelay(BLINK_ON_MS);
            rgb_led_set_color(0x000000);
            rt_thread_mdelay(BLINK_OFF_MS);
        }
    }
#endif
    return 0;
}
