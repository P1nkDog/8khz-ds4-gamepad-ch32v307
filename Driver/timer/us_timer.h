/********************************** (C) COPYRIGHT  *******************************
* File Name          : us_timer.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/24
* Description        : TIM2 微秒定时器接口声明
*********************************************************************************/

#ifndef US_TIMER_H
#define US_TIMER_H

#include <stdint.h>

//初始化
void us_timer_init(void);
//时间读取
uint32_t micros(void);

#endif //US_TIMER_H