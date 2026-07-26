/********************************** (C) COPYRIGHT  *******************************
* File Name          : input.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : 手柄GPIO按键和摇杆ADC读取接口定义
*********************************************************************************/

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

//--------------------------------------------------------------------+
// 数据结构
//--------------------------------------------------------------------+

// 摇杆数据
typedef struct{
    uint16_t lx;
    uint16_t ly;
    uint16_t rx;
    uint16_t ry;
} JoystickData;






//--------------------------------------------------------------------+
// GPIO按键定义
//--------------------------------------------------------------------+
#define INPUT_MASK_PE6      (1U <<  0)
#define INPUT_MASK_PE5      (1U <<  1)
#define INPUT_MASK_PE4      (1U <<  2)
#define INPUT_MASK_PE3      (1U <<  3)
#define INPUT_MASK_PE2      (1U <<  4)
#define INPUT_MASK_PE1      (1U <<  5)
#define INPUT_MASK_PE0      (1U <<  6)
#define INPUT_MASK_PB5      (1U <<  7)
#define INPUT_MASK_PB4      (1U <<  8)
#define INPUT_MASK_PB3      (1U <<  9)
#define INPUT_MASK_PD7      (1U << 10)
#define INPUT_MASK_PD6      (1U << 11)
#define INPUT_MASK_PD5      (1U << 12)
#define INPUT_MASK_PD4      (1U << 13)
#define INPUT_MASK_PD3      (1U << 14)
#define INPUT_MASK_PD2      (1U << 15)
#define INPUT_MASK_PD1      (1U << 16)
#define INPUT_MASK_PC12     (1U << 17)
#define INPUT_MASK_PC10     (1U << 18)


//--------------------------------------------------------------------+
// 接口定义
//--------------------------------------------------------------------+

//初始化输入硬件（GPIO，ADC）
void input_init(void);
//读取摇杆
void input_read_joystick(JoystickData *data);
//读取GPIO按键
void input_read_buttons(uint32_t *buttons);


#endif //INPUT_H