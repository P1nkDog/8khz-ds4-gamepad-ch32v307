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

    //定时器基础配置
    TIM_TimeBaseStructure.TIM_Period = 0xFFFF;
    TIM_TimeBaseStructure.TIM_Prescaler = 143;
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
    uint32_t overflow;
    uint16_t cnt;

    //读取两次确保一致
    do{
        overflow = tim2_overflow_count;
        cnt = TIM_GetCounter(TIM2);
    }while(overflow != tim2_overflow_count);

    return (overflow << 16) | cnt;
}