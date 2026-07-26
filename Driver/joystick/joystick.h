/********************************** (C) COPYRIGHT  *******************************
* File Name          : joystick.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/10
* Description        : 摇杆ADC数据处理头文件
*********************************************************************************/

#ifndef JOYSTICK_H
#define JOYSTICK_H

#define deadzonepersent 5

//--------------------------------------------------------------------+
// 数据结构
//--------------------------------------------------------------------+
typedef struct{
    uint16_t min;
    uint16_t center;
    uint16_t max;
} JoystickCalibration;

typedef struct{
    uint16_t lx;
    uint16_t ly;
    uint16_t rx;
    uint16_t ry;
} JoystickOutput;



//--------------------------------------------------------------------+
// 接口定义
//--------------------------------------------------------------------+

//摇杆初始化
void joystick_init(void);

//摇杆轴校准
void joystick_set_calibration();











#endif //JOYSTICK_H

