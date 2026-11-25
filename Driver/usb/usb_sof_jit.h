/********************************** (C) COPYRIGHT *******************************
* File Name          : usb_sof_jit.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/11/25
* Description        : USBHS SOF 相位锁定 + TIM4 微帧内晚装填（JIT）接口
*
*   设计要点：
*     主机每 125us 发一次 SOF。手柄上报周期是微帧的整数倍（8kHz=1、4kHz=2、
*     2kHz=4、1kHz=8、500Hz=16、250Hz=32）。在"上一帧被主机 ACK"之后开始数 SOF，
*     数到第 n 个 SOF 时启动 TIM4 单脉冲，85us 后置 pack_due，主循环立刻抓最新
*     DMA Bank、组包并交给 EP1 IN —— 数据在下一个微帧起点前就位，既不吃旧样本
*     也不会因为提前装填而在端点里干等一整个周期。
*********************************************************************************/

#ifndef USB_SOF_JIT_H
#define USB_SOF_JIT_H

#include <stdbool.h>
#include <stdint.h>

//--------------------------------------------------------------------+
// 微帧内偏置（n >= 2 档位：4kHz ~ 250Hz）
//   在"目标微帧的前一个 SOF"之后 85us 触发晚装填；
//   125us - 85us = 40us 余量，留给主循环读 DMA、摇杆映射、组包与写 EP1 DMA。
//--------------------------------------------------------------------+
#define USB_SOF_JIT_OFFSET_US       85u

//--------------------------------------------------------------------+
// 8kHz 专用偏置（n == 1 档位）
//   n == 1 时第 k 与 k+1 微帧之间没有中间 SOF 可用，只能在当前微帧 EP1 IN ACK
//   （约 +20us）之后直接延时 70us 触发晚装填：20us + 70us = 90us，
//   距下一微帧起点（125us）仍留 35us 余量。
//--------------------------------------------------------------------+
#define USB_SOF_JIT_8K_OFFSET_US    70u

//TIM4 预分频：APB1 定时器时钟 144MHz / 144 = 1MHz（1 tick = 1us）
#define USB_SOF_JIT_PSC         143u

//初始化 TIM4 单脉冲时基与 TIM4_IRQn（不启动计数器）
void usb_sof_jit_init(void);

//SOF 中断钩子：微帧倒计数 + 到点启动 TIM4（在 USBHS ISR 内调用）
void usb_sof_jit_on_sof(void);

//EP1 IN 完成钩子：重锁相位，按当前轮询档位重装倒计数（在 USBHS ISR 内调用）
void usb_sof_jit_on_ep1_in_complete(void);

//主循环门控：未锁相时直通（允许发首帧），锁相后只在 TIM4 到期时为真
bool usb_sof_jit_is_due(void);

//消费到期标志（主循环在真正抓样组包前调用）
void usb_sof_jit_consume_due(void);

//诊断计数（只增不减，供 MSP/串口观测）
uint32_t usb_sof_jit_get_sof_count(void);
uint32_t usb_sof_jit_get_fire_count(void);
uint32_t usb_sof_jit_get_miss_count(void);

#endif //USB_SOF_JIT_H
