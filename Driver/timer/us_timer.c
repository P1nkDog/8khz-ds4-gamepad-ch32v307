/********************************** (C) COPYRIGHT  *******************************
* File Name          : us_timer.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/24
* Description        : TIM2 微秒定时器
*********************************************************************************/

#include "ch32v30x.h"
#include "us_timer.h"

static volatile uint32_t tim2_overflow_count = 0;

//--------------------------------------------------------------------+
// tim2溢出中断处理
//--------------------------------------------------------------------+

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM2_IRQHandler(void){
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET){
        tim2_overflow_count++;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

//--------------------------------------------------------------------+
// tim2初始化
//--------------------------------------------------------------------+

void us_timer_init(void){
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    //使能tim2时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    //按时钟树计算预分频：TIM2 时钟 = PCLK1 × (APB1 分频≠1 时 ×2)
    RCC_ClocksTypeDef clks;
    RCC_GetClocksFreq(&clks);
    uint32_t tim_clk = (clks.HCLK_Frequency == clks.PCLK1_Frequency)
                     ? clks.PCLK1_Frequency
                     : (clks.PCLK1_Frequency * 2u);
    uint32_t psc = tim_clk / 1000000u;
    if (psc == 0u)    psc = 1u;         //保护：时钟低于 1MHz
    if (psc > 65536u) psc = 65536u;

    //定时器基础配置
    TIM_TimeBaseStructure.TIM_Period = 0xFFFF;
    TIM_TimeBaseStructure.TIM_Prescaler = (uint16_t)(psc - 1u);
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    //清除更新中断标志
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    //配置NVIC
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    //使能更新中断
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    //启动定时器
    TIM_Cmd(TIM2, ENABLE);
}


//--------------------------------------------------------------------+
// 读取当前时间
//--------------------------------------------------------------------+
uint32_t micros(void){
    uint32_t primask;
    uint32_t overflow;
    uint32_t cnt;

    //关总中断，保证 overflow 与计数器取自同一时刻
    __asm volatile ("csrrc %0, 0x800, %1" : "=r"(primask) : "r"(0x88u) : "memory");

    overflow = tim2_overflow_count;
    cnt      = TIM_GetCounter(TIM2);

    //硬件已溢出、但 TIM2 中断被挡住还没执行：计数值已回绕到小值，
    //而 overflow 仍是旧的。此时 UIF 一定置位，据此补 1。
    if ((TIM_GetFlagStatus(TIM2, TIM_FLAG_Update) != RESET) && (cnt < 0x8000u)) {
        overflow++;
    }

    //按原状态恢复中断（原来关着就保持关）
    __asm volatile ("csrs 0x800, %0" : : "r"(primask & 0x88u) : "memory");

    return (overflow << 16) | cnt;
}