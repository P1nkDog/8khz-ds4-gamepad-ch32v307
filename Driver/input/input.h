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
// 方向键
#define INPUT_MASK_UP        (1U <<  0)   // PE9  网络UP
#define INPUT_MASK_RIGHT     (1U <<  1)   // PE8  网络RIGHT
#define INPUT_MASK_DOWN      (1U <<  2)   // PE11 网络DOWN
#define INPUT_MASK_LEFT      (1U <<  3)   // PE10 网络LEFT
// 面键（网表命名 A/B/X/Y）
#define INPUT_MASK_BTN_X     (1U <<  4)   // PB11 网络X
#define INPUT_MASK_BTN_A     (1U <<  5)   // PE13 网络A
#define INPUT_MASK_BTN_B     (1U <<  6)   // PE14 网络B
#define INPUT_MASK_BTN_Y     (1U <<  7)   // PE15 网络Y
// 肩键
#define INPUT_MASK_L1        (1U <<  8)   // PE7  网络L1
#define INPUT_MASK_R1        (1U <<  9)   // PB10 网络R1
#define INPUT_MASK_L2        (1U << 10)   // PB0  网络L2
#define INPUT_MASK_R2        (1U << 11)   // PB1  网络R2
// 摇杆按下
#define INPUT_MASK_L3        (1U << 12)   // PA5  JS2按键（左摇杆）
#define INPUT_MASK_R3        (1U << 13)   // PE1  JS1按键（右摇杆）
// 系统键
#define INPUT_MASK_PS        (1U << 14)   // PE12 网络PS
#define INPUT_MASK_SELECT    (1U << 15)   // PB12 SW2
#define INPUT_MASK_START     (1U << 16)   // PB14 SW4
#define INPUT_MASK_TOUCHPAD  (1U << 17)   // PB13 SW3
// 扩展按键（暂不映射到DS4报告，保留读取）
#define INPUT_MASK_BTN1      (1U << 18)   // PD3
#define INPUT_MASK_BTN2      (1U << 19)   // PD4
#define INPUT_MASK_BTN3      (1U << 20)   // PD5
#define INPUT_MASK_BTN4      (1U << 21)   // PD6
#define INPUT_MASK_BTN5      (1U << 22)   // PD7
#define INPUT_MASK_BTN6      (1U << 23)   // PB3
#define INPUT_MASK_BTN7      (1U << 24)   // PB4
#define INPUT_MASK_BTN8      (1U << 25)   // PE0


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