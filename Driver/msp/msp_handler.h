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

// 半包超时轮询（由 cdc_process 每轮调用）
//   非 IDLE 状态下超过 50ms 没有新字节 → 丢弃半包回到 IDLE，
//   避免主机半包断连后状态机把后续正常请求当成 payload 吞掉。
void msp_poll(void);

// 处理一个字节（由 cdc_process 调用）
void msp_process_byte(uint8_t byte);

#endif // MSP_HANDLER_H