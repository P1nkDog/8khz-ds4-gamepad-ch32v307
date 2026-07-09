/********************************** (C) COPYRIGHT  *******************************
* File Name          : input.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : 手柄按键ADC读取接口定义
*********************************************************************************/

#ifndef _INPUT_H_
#define _INPUT_H_

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

// 标准按键数据
typedef struct{
    uint8_t dpad;
    uint8_t l2;
    uint8_t r2;
    uint16_t buttons;
} ButtonData;

//拓展按键数据
typedef struct{
    uint16_t extra;
} ExtraButtonData;

//--------------------------------------------------------------------+
// 十字键定义
//--------------------------------------------------------------------+
#define INPUT_HAT_UP        0x00
#define INPUT_HAT_UPRIGHT   0x01
#define INPUT_HAT_RIGHT     0x02
#define INPUT_HAT_DOWNRIGHT 0x03
#define INPUT_HAT_DOWN      0x04
#define INPUT_HAT_DOWNLEFT  0x05
#define INPUT_HAT_LEFT      0x06
#define INPUT_HAT_UPLEFT    0x07
#define INPUT_HAT_NOTHING   0x0F

//--------------------------------------------------------------------+
// 标准按键定义
//--------------------------------------------------------------------+
#define INPUT_MASK_A        (1U <<  0)
#define INPUT_MASK_B        (1U <<  1)
#define INPUT_MASK_X        (1U <<  2)
#define INPUT_MASK_Y        (1U <<  3)
#define INPUT_MASK_L1       (1U <<  4)
#define INPUT_MASK_R1       (1U <<  5)
#define INPUT_MASK_SELECT   (1U <<  6)
#define INPUT_MASK_START    (1U <<  7)
#define INPUT_MASK_L3       (1U <<  8)
#define INPUT_MASK_R3       (1U <<  9)
#define INPUT_MASK_PS       (1U << 10)
#define INPUT_MASK_TP       (1U << 11)

//--------------------------------------------------------------------+
// 拓展按键定义
//--------------------------------------------------------------------+
#define INPUT_MASK_A1        (1U <<  0)
#define INPUT_MASK_A2        (1U <<  1)
#define INPUT_MASK_A3        (1U <<  2)
#define INPUT_MASK_A4        (1U <<  3)
#define INPUT_MASK_A5        (1U <<  4)
#define INPUT_MASK_A6        (1U <<  5)
#define INPUT_MASK_A7        (1U <<  6)
#define INPUT_MASK_A8        (1U <<  7)

//--------------------------------------------------------------------+
// 接口定义
//--------------------------------------------------------------------+

//初始化输入硬件（GPIO，ADC）
void input_init(void);
//读取摇杆
void input_read_joystick(JoystickData *data);
//读取标准按键
void input_read_buttons(ButtonData *data);
//读取拓展按键
void input_read_extrabuttons(ExtraButtonData *data);

#endif //_INPUT_H_