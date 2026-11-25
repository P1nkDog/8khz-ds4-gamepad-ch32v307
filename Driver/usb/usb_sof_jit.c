/********************************** (C) COPYRIGHT *******************************
* File Name          : usb_sof_jit.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/11/25
* Description        : USBHS SOF 相位锁定 + TIM4 微帧内晚装填（JIT）实现
*
*   时序拓扑（以 1kHz / bInterval=8 为例）：
*
*     SOF#0        SOF#1 ... SOF#7        SOF#8            SOF#9
*       |            |         |            |                |
*       ACK 上一帧 --> 倒计数 8 --> 倒计数 1 --> 倒计数 0      |
*                                              |             |
*                                         启动 TIM4(+85us)   |
*                                              |             |
*                                        pack_due -> 组包 --> EP1 IN 事务
*                                                             |
*                                                        下一帧 ACK
*
*   关键约定：
*     1) SOF 是纯硬件路径，不向 TinyUSB 事件队列投递（全仓无 tud_sof_cb 消费者），
*        避免每 125us 往 16 槽队列灌一个没人取的事件。
*     2) 未锁相时 usb_sof_jit_is_due() 恒为 true，保证枚举完成后能发出第一帧；
*        第一帧被 ACK 触发的 on_ep1_in_complete() 才真正开始锁相。
*     3) 主机停轮询（挂起 / 拔出）会让 EP1 IN 完成事件消失，靠 SOF 看门狗
*        在 40 个微帧（5ms，大于最低档 4ms 周期）后自动退锁，回到直通模式。
*********************************************************************************/

#include "usb_sof_jit.h"

#include "ch32v30x.h"
#include "config.h"

//--------------------------------------------------------------------+
// 看门狗：连续多少个 SOF 没等到 EP1 IN 完成就退锁（5ms > 最低档 4ms 周期）
//--------------------------------------------------------------------+
#define USB_SOF_JIT_IDLE_SOF_LIMIT      40u

//微帧间隔数上限（250Hz -> 32）
#define USB_SOF_JIT_MICROFRAME_MAX      32u

//--------------------------------------------------------------------+
// 状态
//--------------------------------------------------------------------+

//是否已锁相（首帧 ACK 之前为 false，此时主循环直通装填）
static volatile bool s_sof_locked;

//是否真的收到过至少一次 SOF 中断
//  安全网：本工程此前从未在硬件上验证过 SOF_ACT_EN 的实际上报形态。万一 SOF 通路
//  不成立（s_sof_irq_count 永远为 0），锁相后主循环会永远等不到 pack_due 而彻底停发。
//  因此"从未见过 SOF"时 is_due() 仍直通，退化为原来的满速上报；一旦见过 SOF，
//  立即进入严格相位锁定模式。
static volatile bool s_sof_seen;

//本周期的 TIM4 是否已启动（防止同一轮询周期重复触发）
static volatile bool s_cycle_armed;

//距离下次装填还剩几个微帧
static volatile uint8_t s_sof_countdown;

//晚装填到期标志（TIM4 中断置位，主循环消费）
static volatile bool s_pack_due;

//连续未发生 EP1 IN 完成的 SOF 计数（退锁看门狗）
static volatile uint8_t s_idle_sof_watchdog;

//诊断计数
static volatile uint32_t s_sof_irq_count;
static volatile uint32_t s_jit_fire_count;
static volatile uint32_t s_jit_late_miss_count;

//--------------------------------------------------------------------+
// 初始化
//--------------------------------------------------------------------+

void usb_sof_jit_init(void)
{
    TIM_TimeBaseInitTypeDef tim_init = {0};
    NVIC_InitTypeDef nvic_init = {0};

    s_sof_locked          = false;
    s_sof_seen            = false;
    s_cycle_armed         = false;
    s_sof_countdown       = 0u;
    s_pack_due            = false;
    s_idle_sof_watchdog   = 0u;
    s_sof_irq_count       = 0u;
    s_jit_fire_count      = 0u;
    s_jit_late_miss_count = 0u;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    //144MHz / (PSC+1=144) = 1MHz -> 1 tick = 1us；ARR = 85-1 -> 85us 后更新
    tim_init.TIM_Period        = (uint16_t)(USB_SOF_JIT_OFFSET_US - 1u);
    tim_init.TIM_Prescaler     = (uint16_t)USB_SOF_JIT_PSC;
    tim_init.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_init.TIM_CounterMode   = TIM_CounterMode_Up;
    tim_init.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM4, &tim_init);

    //单脉冲：更新事件后自动清 CEN，不会连续触发
    TIM_SelectOnePulseMode(TIM4, TIM_OPMode_Single);

    //只有计数器真实溢出才算更新：软件写 UG 不产生多余的更新中断
    TIM_UpdateRequestConfig(TIM4, TIM_UpdateSource_Regular);

    TIM_ClearFlag(TIM4, TIM_FLAG_Update);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
    TIM4->CNT = 0u;
    TIM_Cmd(TIM4, DISABLE);

    //抢占优先级 1：低于 USBHS(0)，高于 DMA1_Channel1(2) 与主循环
    nvic_init.NVIC_IRQChannel                   = TIM4_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 1;
    nvic_init.NVIC_IRQChannelSubPriority        = 0;
    nvic_init.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic_init);
}

//--------------------------------------------------------------------+
// SOF：微帧倒计数 + 到点启动 TIM4 单脉冲
//--------------------------------------------------------------------+

void usb_sof_jit_on_sof(void)
{
    s_sof_seen = true;
    s_sof_irq_count++;

    if (!s_sof_locked)
    {
        return;
    }

    //上一轮的到期标志还没被主循环消费：说明主循环单圈耗时超支
    if (s_pack_due)
    {
        s_jit_late_miss_count++;
    }

    //主机停轮询 / 挂起：EP1 IN 完成事件不再来，自动退锁回直通模式
    if (++s_idle_sof_watchdog > (uint8_t)USB_SOF_JIT_IDLE_SOF_LIMIT)
    {
        s_sof_locked    = false;
        s_cycle_armed   = false;
        s_pack_due      = false;
        s_sof_countdown = 0u;
        return;
    }

    if (s_sof_countdown > 0u)
    {
        s_sof_countdown--;
    }

    if ((s_sof_countdown == 0u) && s_cycle_armed)
    {
        s_cycle_armed = false;
        //显式装载"多微帧档位"的微帧内偏置：上一轮可能是 8kHz 档改写的 70us
        TIM_SetAutoreload(TIM4, (uint16_t)(USB_SOF_JIT_OFFSET_US - 1u));
        TIM4->CNT = 0u;
        //CH32 的 TIM 寄存器名是 CTLR1/TIM_CEN，这里用标准库 TIM_Cmd 等价置 CEN，
        //单脉冲模式下更新事件会硬件自动清 CEN，不需要软件复位。
        TIM_Cmd(TIM4, ENABLE);
    }
}

//--------------------------------------------------------------------+
// EP1 IN 完成：主机已 ACK 上一帧，重锁相位
//--------------------------------------------------------------------+

void usb_sof_jit_on_ep1_in_complete(void)
{
    uint32_t period_us = config_get_poll_interval_us();
    uint32_t n = period_us / 125u;

    if (n == 0u)
    {
        n = 1u;
    }
    if (n > USB_SOF_JIT_MICROFRAME_MAX)
    {
        n = USB_SOF_JIT_MICROFRAME_MAX;
    }

    s_sof_locked        = true;
    s_idle_sof_watchdog = 0u;

    if (n == 1u)
    {
        //8kHz 档：第 k 与 k+1 微帧之间没有中间 SOF，无法等下一个 SOF 再起定时器，
        //  只能在当前微帧 ACK 后立刻启动 70us 单脉冲（ACK 本身约在 +20us），
        //  使新帧在下一微帧 SOF 到来前约 35us 就绪。
        s_sof_countdown = 0u;
        s_cycle_armed   = false;
        TIM_SetAutoreload(TIM4, (uint16_t)(USB_SOF_JIT_8K_OFFSET_US - 1u));
        TIM4->CNT = 0u;
        TIM_Cmd(TIM4, ENABLE);
    }
    else
    {
        //4kHz ~ 250Hz 档（n >= 2）：倒数 n-1 个 SOF 后，在"目标微帧的前一个 SOF"启动
        //  85us 单脉冲，使新帧在目标微帧 SOF 到来前 40us 就绪（修正原 n 造成的整微帧滞后）。
        s_sof_countdown = (uint8_t)(n - 1u);
        s_cycle_armed   = true;
    }
}

//--------------------------------------------------------------------+
// TIM4 更新中断：晚装填到期
//--------------------------------------------------------------------+

void TIM4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
        s_pack_due = true;
        s_jit_fire_count++;
    }
}

//--------------------------------------------------------------------+
// 主循环接口
//--------------------------------------------------------------------+

bool usb_sof_jit_is_due(void)
{
    //未锁相（枚举刚完成 / 刚刚退锁）：直通，允许发首帧
    if (!s_sof_locked)
    {
        return true;
    }

    //安全网：SOF 通路始终没有出现（s_sof_seen 恒为 false）时也直通，
    //  避免"锁了相却永远等不到 TIM4 到期"造成完全停发。
    if (!s_sof_seen)
    {
        return true;
    }

    return s_pack_due;
}

void usb_sof_jit_consume_due(void)
{
    s_pack_due = false;
}

//--------------------------------------------------------------------+
// 诊断接口
//--------------------------------------------------------------------+

uint32_t usb_sof_jit_get_sof_count(void)
{
    return s_sof_irq_count;
}

uint32_t usb_sof_jit_get_fire_count(void)
{
    return s_jit_fire_count;
}

uint32_t usb_sof_jit_get_miss_count(void)
{
    return s_jit_late_miss_count;
}
