/********************************** (C) COPYRIGHT  *******************************
* File Name          : msp_handler.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/30
* Description        : MSP v1 协议处理接口声明
*********************************************************************************/

#ifndef MSP_HANDLER_H
#define MSP_HANDLER_H

#include <stdint.h>
#include "msp_protocol.h"

//--------------------------------------------------------------------+
// 接口声明
//--------------------------------------------------------------------+

// 初始化 MSP 状态机
void msp_init(void);

// 处理一个字节（由 cdc_process 调用）1
void msp_process_byte(uint8_t byte);

#endif // MSP_HANDLER_H