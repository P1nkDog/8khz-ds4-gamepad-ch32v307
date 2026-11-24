/********************************** (C) COPYRIGHT  *******************************
* File Name          : input_adc_dma.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/11/25
* Description        : 摇杆双ADC规则同步 + TIM3定时触发 + DMA1双Bank采样接口
*********************************************************************************/

#ifndef INPUT_ADC_DMA_H
#define INPUT_ADC_DMA_H

#include <stdint.h>

#include "input.h"

/*--------------------------------------------------------------------+
 * 模拟扳机预留槽位（Rank 3）
 *   0 = 只采 2 个 Rank（LX/LY + RX/RY），PB0/PB1 保持数字上拉输入；
 *   1 = 追加 Rank 3：ADC1 采 PB0/ADC_Channel_8（L2），ADC2 采 PB1/ADC_Channel_9（R2）。
 *
 *   ⚠ 打开这个开关前必须同步处理两件事，否则会得到"永远是按下"的扳机：
 *     1) PB0/PB1 会被本模块改成 GPIO_Mode_AIN（切断内部上拉）；
 *     2) input_read_buttons() 仍按数字电平读 PB0/PB1（读到 0 = 按下），
 *        必须改成用模拟值 + 滞回阈值合成 INPUT_MASK_L2 / INPUT_MASK_R2。
 *   当前硬件仍是数字扳机，故保持 0。
 *--------------------------------------------------------------------*/
#define INPUT_ADC_ENABLE_ANALOG_TRIGGERS 0

/*每个 Bank 覆盖的规则组长度：2（摇杆）或 3（摇杆 + 模拟扳机）*/
#define INPUT_ADC_RANK_COUNT (INPUT_ADC_ENABLE_ANALOG_TRIGGERS ? 3u : 2u)

/* 校准采中心时等待新样本的超时（微秒）
 *   每次 TIM3 触发都会写满一个 Bank（2 个 Rank），HT/TC 交替上报，
 *   序号以 32kHz 递增 ≈ 31.25µs 一个新样本；200µs 留约 6 倍余量，
 *   DMA 未启动 / 调试器打断时也能退出，不会把校准流程挂死。 */
#define INPUT_ADC_CALIB_WAIT_TIMEOUT_US 200u

/*初始化双ADC同步采样 + TIM3触发 + DMA1_Channel1（必须在 button_gpio_init() 之后调用，
  否则按键初始化里的 GPIO_Mode_IPU 会把 PA5/PA6/PC4 的模拟配置覆盖掉）*/
void input_adc_dma_init(void);

/*读取最新一个已完成 Bank 的 4 轴原始值（已加各 ADC 的校准偏移并钳位到 0~4095）*/
void input_adc_dma_read(JoystickData *data);

/*当前已完成 Bank 的序号（每个 Bank 完成 +1，用于判断"有没有新样本"）*/
uint32_t input_adc_dma_get_sequence(void);

/*等待序号发生变化（超时兜底），返回等待结束时的序号*/
uint32_t input_adc_dma_wait_next(uint32_t prev_seq, uint32_t timeout_us);

#if INPUT_ADC_ENABLE_ANALOG_TRIGGERS
/*读取 Rank 3 的扳机原始值（仅在 INPUT_ADC_ENABLE_ANALOG_TRIGGERS = 1 时可用）*/
void input_adc_dma_read_triggers(uint16_t *l2_raw, uint16_t *r2_raw);
#endif

#endif //INPUT_ADC_DMA_H
