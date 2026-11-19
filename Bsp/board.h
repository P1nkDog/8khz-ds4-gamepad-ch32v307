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

// v0.2.0 硬件：模板遗留的 PA0(LED) / PA2(KEY) 在板上未引出，且 PA2 已分配给
// USART2_TX（调试串口），故此处不再定义，board_init() 也不再初始化这两个引脚。
//
// 调试串口：USART2_TX = PA2（见 Debug/debug.h 的 DEBUG_UART2）

// TODO UART port

#ifdef __cplusplus
 }
#endif

#endif
