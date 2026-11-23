/********************************** (C) COPYRIGHT  *******************************
* File Name          : input.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : 手柄GPIO按键和摇杆ADC读取
*********************************************************************************/

#include "input.h"
#include "ch32v30x.h"

// 摇杆校准值
static int16_t adc_calibrattion_val;

// 按键消抖状态：最近 4 帧原始位图 + 最终输出的稳定态
static uint32_t btn_hist[4];
static uint8_t  btn_hist_idx;
static uint32_t btn_stable;

//--------------------------------------------------------------------+
// ADC初始化
//--------------------------------------------------------------------+
static void adc_function_init(void)
{
	ADC_InitTypeDef ADC_InitStructure={0};
	GPIO_InitTypeDef GPIO_InitStructure={0};

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE );
    // ADC 时钟 = PCLK2 / 8。Bsp/system_ch32v30x.c 的 SetSysClockTo144_HSE() 里
    // PCLK2 已设为 HCLK/2 = 72MHz，因此 ADC = 72/8 = 9MHz，落在 CH32V307
    // 数据手册 fADC ≤ 14MHz 的规格内。
    // （本库 RCC_ADCCLKConfig() 只支持 /2、/4、/6、/8，CFGR0.ADCPRE 仅 2 位，没有 Div16。
    //   旧配置 PCLK2=144MHz 时最低只能到 18MHz，超规格 28%，会牺牲有效位数与通道隔离度。
    //   若日后把 PCLK2 改回 144MHz，必须同步修改这段说明。）
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    // PA5/PA6 = 右摇杆 X/Y（ADC5/ADC6）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PC4/PC5 = 左摇杆 X/Y（ADC14/ADC15）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

	ADC_DeInit(ADC1);
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfChannel = 1;
	ADC_Init(ADC1, &ADC_InitStructure);

	// ADC_DMACmd(ADC1, ENABLE);
	ADC_Cmd(ADC1, ENABLE);

	ADC_BufferCmd(ADC1, DISABLE);   //disable buffer
	ADC_ResetCalibration(ADC1);
	while(ADC_GetResetCalibrationStatus(ADC1));
	ADC_StartCalibration(ADC1);
	while(ADC_GetCalibrationStatus(ADC1));
	adc_calibrattion_val = Get_CalibrationValue(ADC1);	
	
}


//--------------------------------------------------------------------+
// ADC读取原始值
//--------------------------------------------------------------------+

//ADC 各通道最后一次有效转换结果（超时兜底用），按通道号索引
static uint16_t adc_last_valid[16];

//单次转换的等待圈数上限
//  84 个 ADC 周期（71.5 采样 + 12.5 转换）@9MHz ≈ 9.3us，对应 144MHz 内核约 1340 周期；
//  轮询循环每圈约 6 周期，正常情况几十圈就出结果。取 10000 圈（约 400us）
//  留出 40 倍余量，同时保证"ADC 无响应"时不会把整机拖死。
#define ADC_EOC_TIMEOUT_LOOPS 10000u

static uint16_t adc_read_raw(uint8_t channel){
    ADC_RegularChannelConfig(ADC1,channel,1,ADC_SampleTime_71Cycles5);
    ADC_SoftwareStartConvCmd(ADC1,ENABLE);

    //有界等待：ADC 时钟未使能 / ADC 被意外复位时，无超时的死等会让整个手柄静默挂死
    //  （USB 还在、但一帧报告都不出，现场极难判断）。超时返回该通道上一次有效值，
    //  让主循环继续跑下去，上位机还能连上来查出问题。
    uint32_t guard = ADC_EOC_TIMEOUT_LOOPS;
    while (!ADC_GetFlagStatus(ADC1,ADC_FLAG_EOC)){
        if (--guard == 0u){
            return adc_last_valid[channel & 0x0Fu];
        }
    }

    uint16_t value = ADC_GetConversionValue(ADC1);
    adc_last_valid[channel & 0x0Fu] = value;
    return value;
}

//--------------------------------------------------------------------+
// ADC读取校准值
//--------------------------------------------------------------------+
static uint16_t adc_read_calibrated(uint8_t channel){
    int32_t raw_value = adc_read_raw(channel);
    int32_t calibrated_value = raw_value + adc_calibrattion_val;
    if(calibrated_value < 0) calibrated_value = 0;
    if(calibrated_value > 4095) calibrated_value = 4095;
    return (uint16_t)calibrated_value;
}

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
    adc_function_init();
    button_gpio_init();
}


//读取摇杆
void input_read_joystick(JoystickData *data){
    data -> lx = adc_read_calibrated(ADC_Channel_14);   // PC4 左摇杆X
    data -> ly = adc_read_calibrated(ADC_Channel_15);   // PC5 左摇杆Y
    data -> rx = adc_read_calibrated(ADC_Channel_5);    // PA5 右摇杆X
    data -> ry = adc_read_calibrated(ADC_Channel_6);    // PA6 右摇杆Y
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
    // 4 帧一致性消抖（零阻塞）
    //--------------------------------------------------------------------+
    //  机械触点抖动 1~5ms，8kHz 下每 125us 采一次，会把抖动如实上报 8~40 次；
    //  主机侧虽然有去抖，但菜单单次确认、格斗游戏的帧精确输入仍会受影响。
    //  这里保留最近 4 帧的原始位图，4 帧完全一致才把稳定态输出出去：
    //    - 零阻塞：不延时、不等待，只比较 4 个 32bit 变量（约 10 个周期）；
    //    - 8kHz 等效稳定窗 = 4 x 125us = 0.5ms；1kHz = 4ms；250Hz = 16ms。
    //      稳定窗随帧周期等比放大是"帧计数消抖"的固有代价（低回报率档位延迟换来的
    //      是更彻底的抖动滤除）；若需要恒定时间窗，应改成基于 micros() 的时间戳版本。
    //    - 中间态（4 帧不一致）保持上一次的稳定值，不会产生中间抖动。
    btn_hist[btn_hist_idx] = raw;
    btn_hist_idx = (uint8_t)((btn_hist_idx + 1u) & 0x03u);

    if (btn_hist[0] == btn_hist[1] &&
        btn_hist[1] == btn_hist[2] &&
        btn_hist[2] == btn_hist[3]) {
        btn_stable = raw;
    }

    *buttons = btn_stable;
}