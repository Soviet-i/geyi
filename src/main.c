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
#include "drivers/rt_drv_pwm.h"

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

static void rgb_led_init(void)
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
        RT_ASSERT(0);
    }
}

static void rgb_led_set_color(uint32_t color)
{
    struct rt_rgbled_configuration configuration;
    configuration.color_rgb = color;
    rt_device_control(rgbled_device, PWM_CMD_SET_COLOR, &configuration);
}

int main(void)
{
    rt_kprintf("\n[%s] v%s boot ok\n", APP_NAME, APP_VERSION);
    rt_kprintf("Hello world!\n");

    rgb_led_init();

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
    return 0;
}
