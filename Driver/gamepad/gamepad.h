/********************************** (C) COPYRIGHT  *******************************
* File Name          : gamepad.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/10
* Description        : GPIO按键映射和摇杆ADC数据处理接口定义
*********************************************************************************/

#ifndef _GAMEPAD_H_
#define _GAMEPAD_H_

#include <stdint.h>

// DPAD掩码
#define GAMEPAD_HAT_UP        0x00
#define GAMEPAD_HAT_UPRIGHT   0x01
#define GAMEPAD_HAT_RIGHT     0x02
#define GAMEPAD_HAT_DOWNRIGHT 0x03
#define GAMEPAD_HAT_DOWN      0x04
#define GAMEPAD_HAT_DOWNLEFT  0x05
#define GAMEPAD_HAT_LEFT      0x06
#define GAMEPAD_HAT_UPLEFT    0x07
#define GAMEPAD_HAT_NOTHING   0x0F

//--------------------------------------------------------------------+
// 数据结构
//--------------------------------------------------------------------+
typedef struct{

    //摇杆数据
    uint16_t lx;
    uint16_t ly;
    uint16_t rx;
    uint16_t ry;

    //方向键
    uint8_t dpad : 4;

    //按键
    uint16_t buttonWest : 1;
    uint16_t buttonSouth : 1;
    uint16_t buttonEast : 1;
    uint16_t buttonNorth : 1;
    uint16_t buttonL1 : 1;
    uint16_t buttonR1 : 1;
    uint16_t buttonL2 : 1;
    uint16_t buttonR2 : 1;
    uint16_t buttonSelect : 1;
    uint16_t buttonStart : 1;
    uint16_t buttonL3 : 1;
    uint16_t buttonR3 : 1;
    uint16_t buttonHome : 1;
    uint16_t buttonTouchpad : 1;
} GamepadState;

//--------------------------------------------------------------------+
// 接口定义
//--------------------------------------------------------------------+

//初始化
void gamepad_init(void);
//数据映射
void gamepad_process(void);
//获取手柄摇杆按键状态
const GamepadState* gamepad_get_state(void);






#endif //_GAMEPAD_H_