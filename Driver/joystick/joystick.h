/********************************** (C) COPYRIGHT  *******************************
* File Name          : joystick.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/10
* Description        : 摇杆ADC数据处理头文件：映射、死区、校准接口
*********************************************************************************/

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdint.h>
#include <stdbool.h>

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

//每帧调用：读摇杆，硬件取反，死区外满量程映射
void joystick_process(void);

//获取映射后的12bit输出
const JoystickOutput* joystick_get_output(void);

//获取摇杆取反后原始值（供msp实时显示）
const uint16_t* joystick_get_raw(void);

//进入校准模式
void joystick_calib_start(void);

//采样中心值
void joystick_calib_sample_center(void);

//开始跟踪摇杆最大最小值
void joystick_calib_track_start(void);

//结束跟踪并保存，返回是否成功
bool joystick_calib_track_end(void);

//取消校准
void joystick_calib_cancel(void);



#endif //JOYSTICK_H

