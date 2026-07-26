/********************************** (C) COPYRIGHT  *******************************
* File Name          : board.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/01
* Description        : 板级引脚定义
*********************************************************************************/

#ifndef BOARD_H_
#define BOARD_H_

#ifdef __cplusplus
 extern "C" {
#endif

// LED: need to wire pin LED1 to PA0 in the J3 header
#define LED_PORT              GPIOA
#define LED_PIN               GPIO_Pin_0
#define LED_STATE_ON          0
#define LED_CLOCK_EN()        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE)

// Button: need to wire pin KEY to PA2 in the J3 header
#define BUTTON_PORT           GPIOA
#define BUTTON_PIN            GPIO_Pin_2
#define BUTTON_STATE_ACTIVE   0
#define BUTTON_CLOCK_EN()     do { } while(0) // same as LED clock, no need to do anything

// TODO UART port

#ifdef __cplusplus
 }
#endif

#endif
