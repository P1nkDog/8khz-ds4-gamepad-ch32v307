/********************************** (C) COPYRIGHT  *******************************
* File Name          : input.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : 手柄GPIO按键和摇杆ADC读取
*********************************************************************************/

#include "input.h"
#include "ch32v30x.h"
#include "config.h"
#include "us_timer.h"
#include "input_adc_dma.h"

//--------------------------------------------------------------------+
// 按键消抖状态（26 键独立时间窗口）
//  旧实现是"最近 4 帧原始位图全等才输出稳定态"，稳定窗随轮询率等比缩放
//  （8kHz = 0.5ms、1kHz = 4ms、250Hz = 16ms），同一个固件换个回报率档位
//  手感就变了。这里改成每键记录"原始电平最后一次变化"的 micros() 时间戳：
//  原始电平与稳定态不一致、且已持续 >= debounce_ms 才翻转，
//  稳定窗是与轮询率无关的恒定毫秒数。
//
//  时间比较一律用 uint32_t 无符号减法 (now - change_us)：micros() 的 32-bit
//  计数器约 71.5 分钟回绕一次，无符号减法在回绕点依然给出正确的"经过时长"。
//--------------------------------------------------------------------+
#define INPUT_DEBOUNCE_KEY_COUNT 26U   // Bit0~17 标准键 + Bit18~25 扩展键

static uint32_t btn_raw_last;                              // 上一次采样的原始位图
static uint32_t btn_stable;                                // 已通过消抖的稳定位图
static uint32_t btn_change_us[INPUT_DEBOUNCE_KEY_COUNT];    // 每键原始电平最后变化时刻

//--------------------------------------------------------------------+
// ADC（已迁移到双 ADC + DMA 模块）
//  摇杆采样改由 input_adc_dma.c 承担：ADC1 + ADC2 规则同步，TIM3 32kHz 触发，
//  DMA1_Channel1 32 位循环双 Bank 搬运。这里不再保留单 ADC 轮询路径，
//  主循环每帧不再为 4 次转换各阻塞约 9.3us（合计 ~37.3us）。
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// GPIO初始化
//--------------------------------------------------------------------+
static void button_gpio_init(void){
    GPIO_InitTypeDef GPIO_InitStructure={0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);     //写 AFIO->PCFR1 前必须先开 AFIO 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

    // 关闭 JTAG-DP、保留 SW-DP：AFIO_PCFR1.SWJ_CFG[2:0] = 010
    // 释放 PB3(JTDO)/PB4(NJTRST)/PA15(JTDI) 作普通 GPIO，
    // PA13(SWDIO)/PA14(SWCLK) 仍是调试口，不会被一起关掉。
    // 标准库只提供 GPIO_Remap_SWJ_Disable（JTAG+SWD 全关），所以这里直接写寄存器。
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_JTAGDISABLE;

    // ---- 低电平有效按键：内部上拉 ----
    // PE1,PE8~PE15（PE0 未使用，PE7 是 L1 高有效，都不在此掩码里）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 |
                                  GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                                  GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 |
                                  GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    // PB0,PB1,PB3,PB11,PB12,PB13,PB14（PB4 未使用，PB10 是 R1 高有效，都不在此掩码里）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3 |
                                  GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PD1~PD7：BTN1~BTN7
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 |
                                  GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // PA7：左摇杆按下（L3，JS2按键）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // ---- 高电平有效按键：内部下拉 ----
    // PE7 = L1，板上无外部下拉电阻，靠内部下拉保证松开时为低电平
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    // PB10 = R1，同上
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

//--------------------------------------------------------------------+
// GPIO读取
//--------------------------------------------------------------------+
static uint8_t gpio_read_pin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin){
    return GPIO_ReadInputDataBit(GPIOx,GPIO_Pin);
}





//--------------------------------------------------------------------+
// 公共接口
//--------------------------------------------------------------------+

//初始化输入硬件（GPIO，ADC）
void input_init(void){
    //先配数字按键，再配模拟引脚：ADC 模块最后把 PA5/PA6/PC4/PC5 落成 GPIO_Mode_AIN，
    //保证模拟输入不会被任何上/下拉配置覆盖
    button_gpio_init();
    input_adc_dma_init();

    //消抖状态清零：基准态从"全松开"开始，避免上电瞬间残留脏时间戳
    btn_raw_last = 0;
    btn_stable   = 0;
    for (uint8_t i = 0; i < INPUT_DEBOUNCE_KEY_COUNT; i++) {
        btn_change_us[i] = 0;
    }
}


//读取摇杆
void input_read_joystick(JoystickData *data){
    //直接从双 ADC DMA 的最新已完成 Bank 读取（已含各 ADC 校准偏移与 0~4095 钳位），
    //不再触发任何转换，也不阻塞等待 EOC
    input_adc_dma_read(data);
}

//读取标准按键
void input_read_buttons(uint32_t *buttons){

    uint32_t raw = 0;

    // 方向键
    if(gpio_read_pin(GPIOE, GPIO_Pin_9)  == 0) raw |= INPUT_MASK_UP;
    if(gpio_read_pin(GPIOE, GPIO_Pin_8)  == 0) raw |= INPUT_MASK_RIGHT;
    if(gpio_read_pin(GPIOE, GPIO_Pin_11) == 0) raw |= INPUT_MASK_DOWN;
    if(gpio_read_pin(GPIOE, GPIO_Pin_10) == 0) raw |= INPUT_MASK_LEFT;

    // 面键 A/B/X/Y
    if(gpio_read_pin(GPIOE, GPIO_Pin_13) == 0) raw |= INPUT_MASK_BTN_A;
    if(gpio_read_pin(GPIOE, GPIO_Pin_14) == 0) raw |= INPUT_MASK_BTN_B;
    if(gpio_read_pin(GPIOE, GPIO_Pin_15) == 0) raw |= INPUT_MASK_BTN_Y;
    if(gpio_read_pin(GPIOB, GPIO_Pin_11) == 0) raw |= INPUT_MASK_BTN_X;

    // 肩键：L1/R1 高电平有效（内部下拉），L2/R2 低电平有效（内部上拉）
    if(gpio_read_pin(GPIOE, GPIO_Pin_7)  == Bit_SET) raw |= INPUT_MASK_L1;
    if(gpio_read_pin(GPIOB, GPIO_Pin_10) == Bit_SET) raw |= INPUT_MASK_R1;
    if(gpio_read_pin(GPIOB, GPIO_Pin_0)  == 0) raw |= INPUT_MASK_L2;
    if(gpio_read_pin(GPIOB, GPIO_Pin_1)  == 0) raw |= INPUT_MASK_R2;

    // 摇杆按下 L3/R3
    if(gpio_read_pin(GPIOA, GPIO_Pin_7)  == 0) raw |= INPUT_MASK_L3;
    if(gpio_read_pin(GPIOE, GPIO_Pin_1)  == 0) raw |= INPUT_MASK_R3;

    // PS / Select / Start / Touchpad
    if(gpio_read_pin(GPIOE, GPIO_Pin_12) == 0) raw |= INPUT_MASK_PS;
    if(gpio_read_pin(GPIOB, GPIO_Pin_12) == 0) raw |= INPUT_MASK_SELECT;   // SW2
    if(gpio_read_pin(GPIOB, GPIO_Pin_14) == 0) raw |= INPUT_MASK_START;    // SW4
    if(gpio_read_pin(GPIOB, GPIO_Pin_13) == 0) raw |= INPUT_MASK_TOUCHPAD; // SW3

    // 扩展按键 BTN1~BTN8（v0.2.0 主板保留，当前扫描但未映射到 DS4 报告）
    //   PD1~PD7 = BTN1~BTN7，PB3 = BTN8（PB3 原为 JTDO，已在 button_gpio_init()
    //   里通过 AFIO_PCFR1.SWJ_CFG = JTAGDISABLE 释放为普通 GPIO，SWD 不受影响）。
    if(gpio_read_pin(GPIOD, GPIO_Pin_1) == 0) raw |= INPUT_MASK_BTN1;
    if(gpio_read_pin(GPIOD, GPIO_Pin_2) == 0) raw |= INPUT_MASK_BTN2;
    if(gpio_read_pin(GPIOD, GPIO_Pin_3) == 0) raw |= INPUT_MASK_BTN3;
    if(gpio_read_pin(GPIOD, GPIO_Pin_4) == 0) raw |= INPUT_MASK_BTN4;
    if(gpio_read_pin(GPIOD, GPIO_Pin_5) == 0) raw |= INPUT_MASK_BTN5;
    if(gpio_read_pin(GPIOD, GPIO_Pin_6) == 0) raw |= INPUT_MASK_BTN6;
    if(gpio_read_pin(GPIOD, GPIO_Pin_7) == 0) raw |= INPUT_MASK_BTN7;
    if(gpio_read_pin(GPIOB, GPIO_Pin_3) == 0) raw |= INPUT_MASK_BTN8;

    //--------------------------------------------------------------------+
    // 26 键独立时间窗口消抖（零阻塞，恒定毫秒级）
    //--------------------------------------------------------------------+
    //  机械触点抖动 1~5ms，8kHz 下每 125us 采一次会把抖动如实上报 8~40 次；
    //  主机侧虽然有去抖，但菜单单次确认、格斗游戏的帧精确输入仍会受影响。
    //  这里不再用"帧计数"，而是给每一路按键独立计时：
    //    - 原始电平变化时只刷新该键的时间戳（不立即翻转）；
    //    - 原始电平与稳定态不一致且持续 >= debounce_ms，才把稳定态翻过去；
    //    - 中间毛刺（持续不足 debounce_ms）会被下一次采样重置时间戳，天然滤除；
    //    - 每键独立，26 个键互不干扰（A 键抖不会拖慢 B 键的报告）。
    //  debounce_ms = 0 时走直通分支：完全不做时间判断，等价于关闭消抖。
    const GamepadSettings *settings =
        (const GamepadSettings *)config_get(CONFIG_ID_GAMEPAD_SETTINGS);
    uint8_t debounce_ms = (settings != NULL) ? settings->debounce_ms : 4U;
    if (debounce_ms > CONFIG_DEBOUNCE_MAX_MS) {
        debounce_ms = CONFIG_DEBOUNCE_MAX_MS;   //配置层已拒收，这里是运行期兜底
    }

    if (debounce_ms == 0U) {
        //关闭消抖：原始电平直接作为稳定态，零延迟
        btn_stable   = raw;
        btn_raw_last = raw;
        *buttons     = btn_stable;
        return;
    }

    const uint32_t debounce_us = (uint32_t)debounce_ms * 1000U;
    const uint32_t now = micros();

    for (uint8_t i = 0; i < INPUT_DEBOUNCE_KEY_COUNT; i++) {
        const uint32_t mask = (1UL << i);

        if ((raw & mask) != (btn_raw_last & mask)) {
            //原始电平刚变化：只记录时刻，等它稳住
            btn_change_us[i] = now;
        } else if (((raw & mask) != (btn_stable & mask)) &&
                   ((uint32_t)(now - btn_change_us[i]) >= debounce_us)) {
            //电平已保持 debounce_ms 不变，且与当前稳定态不同 → 正式翻转这一位
            btn_stable = (btn_stable & ~mask) | (raw & mask);
        }
    }

    btn_raw_last = raw;
    *buttons = btn_stable;
}