/********************************** (C) COPYRIGHT  *******************************
* File Name          : input_adc_dma.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/11/25
* Description        : 摇杆双ADC规则同步 + TIM3 32kHz触发 + DMA1双Bank搬运
*
*   采样拓扑：ADC1（主）与 ADC2（从）规则同步（ADC_Mode_RegSimult）。
*     每个触发沿，两个 ADC 各转换一个 Rank，硬件把两路结果拼成一个 32 位字
*     写进 ADC1->RDATAR（低 16 位 = ADC1，高 16 位 = ADC2），DMA 以 Word 搬运。
*
*   Rank 分配：
*     Rank 1：ADC1 = PC4/ADC_Channel_14（LX）  ↔ ADC2 = PA5/ADC_Channel_5 （RX）
*     Rank 2：ADC1 = PC5/ADC_Channel_15（LY）  ↔ ADC2 = PA6/ADC_Channel_6 （RY）
*     Rank 3：ADC1 = PB0/ADC_Channel_8 （L2）  ↔ ADC2 = PB1/ADC_Channel_9 （R2）  ← 预留
*
*   双 Bank：DMA 循环搬运 2*RANK 个字；HT 表示 bank0 写完、TC 表示 bank1 写完。
*     中断里只记账（bank 号 + 序号），解包与校准加法全部留给主循环。
*********************************************************************************/

#include "input_adc_dma.h"

#include "ch32v30x.h"
#include "us_timer.h"

//--------------------------------------------------------------------+
// 参数
//--------------------------------------------------------------------+

//触发频率：32kHz（周期 31.25us）
#define INPUT_ADC_TRIGGER_HZ        32000u

//采样时间：13.5 个 ADCCLK。ADCCLK = PCLK2/6 = 12MHz 时单通道
//  (13.5 + 12.5) / 12MHz = 2.167us；双 ADC 并行，2 个 Rank 一轮 4.33us。
#define INPUT_ADC_SAMPLE_TIME       ADC_SampleTime_13Cycles5

//上电预置值：摇杆轴填中点，扳机轴填"松开"（0）
#define INPUT_ADC_AXIS_NEUTRAL      2048u
#define INPUT_ADC_TRIGGER_RELEASED  0u

//读撕裂重试次数（正常情况第一次就能过，重试只是兜底）
#define INPUT_ADC_READ_RETRY        3u

//--------------------------------------------------------------------+
// 状态
//--------------------------------------------------------------------+

//DMA 落地缓冲：每字低 16 位 = ADC1，高 16 位 = ADC2
static volatile uint32_t s_dma_buf[2][INPUT_ADC_RANK_COUNT];

//已完成 Bank 的序号：每个 Bank（HT/TC）完成 +1
static volatile uint32_t s_sample_seq;

//最近一个"已写满"的 Bank 下标（0 = 前半，1 = 后半）
static volatile uint8_t s_latest_bank;

//两个 ADC 各自的校准偏移（Get_CalibrationValue() 的返回值）
static int16_t s_cal_offset_adc1;
static int16_t s_cal_offset_adc2;

//--------------------------------------------------------------------+
// 内部函数
//--------------------------------------------------------------------+

//按当前 PCLK2/6 配置 ADCCLK；同时把 4 个（或 6 个）模拟引脚切到 AIN
static void input_adc_dma_pins_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);

    //PA5/PA6 = 右摇杆 X/Y（ADC_Channel_5 / ADC_Channel_6）
    gpio_init.GPIO_Pin  = GPIO_Pin_5 | GPIO_Pin_6;
    gpio_init.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio_init);

    //PC4/PC5 = 左摇杆 X/Y（ADC_Channel_14 / ADC_Channel_15）
    gpio_init.GPIO_Pin  = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOC, &gpio_init);

#if INPUT_ADC_ENABLE_ANALOG_TRIGGERS
    //PB0/PB1 = L2/R2（ADC_Channel_8 / ADC_Channel_9），仅模拟扳机版本启用
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio_init.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOB, &gpio_init);
#endif
}

//统一的 ADC 参数写入（校准阶段与同步阶段各调一次）
static void input_adc_dma_apply_config(ADC_TypeDef *adc, uint32_t mode,
                                       uint32_t trigger, uint8_t scan,
                                       uint8_t channel_count)
{
    ADC_InitTypeDef adc_init = {0};

    adc_init.ADC_Mode               = mode;
    adc_init.ADC_ScanConvMode       = scan;
    adc_init.ADC_ContinuousConvMode = DISABLE;
    adc_init.ADC_ExternalTrigConv   = trigger;
    adc_init.ADC_DataAlign          = ADC_DataAlign_Right;
    adc_init.ADC_NbrOfChannel       = channel_count;
    adc_init.ADC_OutputBuffer       = ADC_OutputBuffer_Disable;
    adc_init.ADC_Pga                = ADC_Pga_1;
    ADC_Init(adc, &adc_init);
}

//DMA1_Channel1：32 位、循环、双 Bank（HT + TC 中断）
static void input_adc_dma_dma_init(void)
{
    DMA_InitTypeDef dma_init = {0};
    NVIC_InitTypeDef nvic_init = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    DMA_Cmd(DMA1_Channel1, DISABLE);
    DMA_DeInit(DMA1_Channel1);
    DMA_ClearITPendingBit(DMA1_IT_GL1 | DMA1_IT_HT1 | DMA1_IT_TC1 | DMA1_IT_TE1);

    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    dma_init.DMA_MemoryBaseAddr     = (uint32_t)&s_dma_buf[0][0];
    dma_init.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma_init.DMA_BufferSize         = 2u * INPUT_ADC_RANK_COUNT;
    dma_init.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma_init.DMA_MemoryDataSize     = DMA_MemoryDataSize_Word;
    dma_init.DMA_Mode               = DMA_Mode_Circular;
    dma_init.DMA_Priority           = DMA_Priority_VeryHigh;
    dma_init.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma_init);

    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC, ENABLE);

    //抢占优先级 2：低于 USB（默认 0）与 TIM2 1MHz 节拍（1），高于主循环
    nvic_init.NVIC_IRQChannel                   = DMA1_Channel1_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 2;
    nvic_init.NVIC_IRQChannelSubPriority        = 0;
    nvic_init.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic_init);
}

//TIM3：PSC=0、ARR=4499 @144MHz → 32.000kHz 更新事件 → TRGO
static void input_adc_dma_timer_init(void)
{
    TIM_TimeBaseInitTypeDef timer_init = {0};
    RCC_ClocksTypeDef clocks;
    uint32_t timer_clk;
    uint32_t divider;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    //TIM3 挂 APB1：分频≠1 时定时器时钟 = PCLK1 x 2（本工程 72MHz x 2 = 144MHz）
    RCC_GetClocksFreq(&clocks);
    timer_clk = (clocks.HCLK_Frequency == clocks.PCLK1_Frequency) ?
                    clocks.PCLK1_Frequency :
                    (clocks.PCLK1_Frequency * 2u);

    //144MHz / 32kHz = 4500 → PSC = 0、ARR = 4499（32.000kHz，误差 0）
    divider = timer_clk / INPUT_ADC_TRIGGER_HZ;
    if (divider == 0u)
    {
        divider = 1u;
    }
    else if (divider > 65536u)
    {
        divider = 65536u;
    }

    timer_init.TIM_Period        = (uint16_t)(divider - 1u);
    timer_init.TIM_Prescaler     = 0u;
    timer_init.TIM_ClockDivision = TIM_CKD_DIV1;
    timer_init.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &timer_init);
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_Cmd(TIM3, DISABLE);
}

//校准偏移：raw + offset 后钳位到 12-bit 量程
static uint16_t input_adc_dma_apply_offset(uint16_t raw, int16_t offset)
{
    int32_t adjusted = (int32_t)raw + (int32_t)offset;

    if (adjusted < 0)
    {
        adjusted = 0;
    }
    else if (adjusted > 4095)
    {
        adjusted = 4095;
    }

    return (uint16_t)adjusted;
}

//--------------------------------------------------------------------+
// 中断：只记账，不解包
//--------------------------------------------------------------------+

void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void DMA1_Channel1_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_HT1) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_HT1);
        s_latest_bank = 0u;
        s_sample_seq++;
    }

    if (DMA_GetITStatus(DMA1_IT_TC1) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_TC1);
        s_latest_bank = 1u;
        s_sample_seq++;
    }
}

//--------------------------------------------------------------------+
// 公共接口
//--------------------------------------------------------------------+

void input_adc_dma_init(void)
{
    uint8_t bank;
    uint8_t rank;

    //0) 缓冲预置：避免上电后第一次读取拿到全 0（看起来像摇杆推到极限）
    for (bank = 0u; bank < 2u; bank++)
    {
        for (rank = 0u; rank < INPUT_ADC_RANK_COUNT; rank++)
        {
            uint16_t initial = (rank < 2u) ? INPUT_ADC_AXIS_NEUTRAL
                                           : INPUT_ADC_TRIGGER_RELEASED;
            s_dma_buf[bank][rank] = ((uint32_t)initial << 16) | initial;
        }
    }
    s_sample_seq  = 0u;
    s_latest_bank = 0u;
    s_cal_offset_adc1 = 0;
    s_cal_offset_adc2 = 0;

    //1) 时钟：ADC 时钟 = PCLK2/6 = 72MHz/6 = 12MHz（手册 fADC <= 14MHz）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_ADC2, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    input_adc_dma_pins_init();

    //2) 独立模式 + 单通道 + 无外部触发，先各自校准
    //   （校准期间 DMA / 触发必须全程关闭，否则会读到校准过程的中间值）
    ADC_DeInit(ADC1);
    ADC_DeInit(ADC2);
    input_adc_dma_apply_config(ADC1, ADC_Mode_Independent, ADC_ExternalTrigConv_None,
                              DISABLE, 1);
    input_adc_dma_apply_config(ADC2, ADC_Mode_Independent, ADC_ExternalTrigConv_None,
                              DISABLE, 1);

    ADC_DMACmd(ADC1, DISABLE);
    ADC_DMACmd(ADC2, DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    ADC_ExternalTrigConvCmd(ADC2, DISABLE);

    ADC_Cmd(ADC1, ENABLE);
    ADC_Cmd(ADC2, ENABLE);
    ADC_BufferCmd(ADC1, DISABLE);
    ADC_BufferCmd(ADC2, DISABLE);

    s_cal_offset_adc1 = Get_CalibrationValue(ADC1);
    s_cal_offset_adc2 = Get_CalibrationValue(ADC2);

    //3) 规则序列 + 规则同步模式
    input_adc_dma_apply_config(ADC1, ADC_Mode_RegSimult, ADC_ExternalTrigConv_T3_TRGO,
                              ENABLE, (uint8_t)INPUT_ADC_RANK_COUNT);
    input_adc_dma_apply_config(ADC2, ADC_Mode_RegSimult, ADC_ExternalTrigConv_None,
                              ENABLE, (uint8_t)INPUT_ADC_RANK_COUNT);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_14, 1, INPUT_ADC_SAMPLE_TIME);  // LX
    ADC_RegularChannelConfig(ADC1, ADC_Channel_15, 2, INPUT_ADC_SAMPLE_TIME);  // LY
    ADC_RegularChannelConfig(ADC2, ADC_Channel_5,  1, INPUT_ADC_SAMPLE_TIME);  // RX
    ADC_RegularChannelConfig(ADC2, ADC_Channel_6,  2, INPUT_ADC_SAMPLE_TIME);  // RY
#if INPUT_ADC_ENABLE_ANALOG_TRIGGERS
    ADC_RegularChannelConfig(ADC1, ADC_Channel_8,  3, INPUT_ADC_SAMPLE_TIME);  // L2
    ADC_RegularChannelConfig(ADC2, ADC_Channel_9,  3, INPUT_ADC_SAMPLE_TIME);  // R2
#endif

    //4) 只让 ADC1 发 DMA 请求（同步模式下 ADC2 的结果由 ADC1->RDATAR 的高 16 位带出）
    ADC_DMACmd(ADC2, DISABLE);
    ADC_DMACmd(ADC1, ENABLE);

    //   主从触发：ADC1 由 TIM3_TRGO 启动；ADC2 在同步模式下由 ADC1 的触发沿带动。
    //   ADC2 的 EXTTRIG 位一并置位（无副作用，与已验证的同类实现保持一致）。
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC2, ENABLE);

    //5) DMA + TIM3 最后启动，保证第一轮触发时缓冲与通道都已就绪
    input_adc_dma_dma_init();
    input_adc_dma_timer_init();

    DMA_Cmd(DMA1_Channel1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

void input_adc_dma_read(JoystickData *data)
{
    uint32_t seq_before;
    uint32_t seq_after;
    uint8_t  bank;
    uint8_t  bank_after;
    uint32_t word_rank1 = 0u;
    uint32_t word_rank2 = 0u;
    uint8_t  attempt;

    if (data == 0)
    {
        return;
    }

    //无撕裂读取：读前读后各取一次序号与 bank 号，一致才认为这一份数据完整。
    //  最新 bank 一定是"已写满"的那一半，DMA 此时正在写另一半，理论上不会撞；
    //  序号比对只是为了挡住"读完第一个字就被中断翻了 bank"的极端窗口。
    for (attempt = 0u; attempt < INPUT_ADC_READ_RETRY; attempt++)
    {
        seq_before = s_sample_seq;
        __asm volatile("" ::: "memory");
        bank = s_latest_bank;
        word_rank1 = s_dma_buf[bank][0];
        word_rank2 = s_dma_buf[bank][1];
        __asm volatile("" ::: "memory");
        seq_after  = s_sample_seq;
        bank_after = s_latest_bank;

        if ((seq_before == seq_after) && (bank == bank_after))
        {
            break;
        }
    }

    //低 16 位 = ADC1（左），高 16 位 = ADC2（右）
    data->lx = input_adc_dma_apply_offset((uint16_t)(word_rank1 & 0xFFFFu), s_cal_offset_adc1);
    data->ly = input_adc_dma_apply_offset((uint16_t)(word_rank2 & 0xFFFFu), s_cal_offset_adc1);
    data->rx = input_adc_dma_apply_offset((uint16_t)(word_rank1 >> 16),       s_cal_offset_adc2);
    data->ry = input_adc_dma_apply_offset((uint16_t)(word_rank2 >> 16),       s_cal_offset_adc2);
}

uint32_t input_adc_dma_get_sequence(void)
{
    return s_sample_seq;
}

uint32_t input_adc_dma_wait_next(uint32_t prev_seq, uint32_t timeout_us)
{
    uint32_t start = micros();

    while (s_sample_seq == prev_seq)
    {
        if ((uint32_t)(micros() - start) >= timeout_us)
        {
            break;
        }
    }

    return s_sample_seq;
}

#if INPUT_ADC_ENABLE_ANALOG_TRIGGERS
void input_adc_dma_read_triggers(uint16_t *l2_raw, uint16_t *r2_raw)
{
    uint32_t word_rank3;
    uint8_t  bank;

    if ((l2_raw == 0) || (r2_raw == 0))
    {
        return;
    }

    bank = s_latest_bank;
    word_rank3 = s_dma_buf[bank][2];

    *l2_raw = input_adc_dma_apply_offset((uint16_t)(word_rank3 & 0xFFFFu), s_cal_offset_adc1);
    *r2_raw = input_adc_dma_apply_offset((uint16_t)(word_rank3 >> 16),       s_cal_offset_adc2);
}
#endif
