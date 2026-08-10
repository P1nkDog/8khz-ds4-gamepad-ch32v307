/********************************** (C) COPYRIGHT  *******************************
* File Name          : joystick.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/10
* Description        : 摇杆ADC数据处理：硬件取反、死区、死区外满量程映射、校准状态机
*********************************************************************************/

#include "joystick.h"
#include "config.h"
#include "input.h"
#include <string.h>

//--------------------------------------------------------------------+
// 常量与轴编号
//--------------------------------------------------------------------+

#define JOYSTICK_OUTPUT_MID 2048
#define JOYSTICK_OUTPUT_MAX 4095
#define DEADZONE_PERCENT_MAX 50

//轴编号
enum{
    AXIS_LX = 0,
    AXIS_LY = 1,
    AXIS_RX = 2,
    AXIS_RY = 3,
};


//--------------------------------------------------------------------+
// 校准状态机
//--------------------------------------------------------------------+

//校准状态
typedef enum{
    CALIB_IDLE = 0,
    CALIB_TRACKING = 1,
} CalibState;


//--------------------------------------------------------------------+
// 静态变量
//--------------------------------------------------------------------+

static CalibState calib_state;
static JoystickOutput joy_out;
static uint16_t raw_logical[4]; //4轴逻辑原始值（取反后）
static uint16_t track_min[4];
static uint16_t track_max[4];


//--------------------------------------------------------------------+
// 内部函数
//--------------------------------------------------------------------+

//读取一个轴的逻辑原始值（取反后）
static uint16_t joystick_read_axis(uint8_t axis, const JoystickData *raw){
    uint16_t v;
    switch(axis){
        case AXIS_LX: v = raw->lx; break;
        case AXIS_LY: v = raw->ly; break;
        case AXIS_RX: v = raw->rx; break;
        case AXIS_RY: v = raw->ry; break;
        default: v = 0; break;
    }
    //硬件方向取反
    if (axis == AXIS_LX || axis == AXIS_RX) {
        v = JOYSTICK_OUTPUT_MAX - v;
    }
    return v;
}

//单轴死区外满量程映射
static uint16_t joystick_map_axis(uint16_t raw, const JoystickCalibration *calib, uint16_t deadzone_present){
    if(deadzone_present > DEADZONE_PERCENT_MAX) deadzone_present = DEADZONE_PERCENT_MAX;

    int32_t center = calib->center;
    int32_t min = calib->min;
    int32_t max = calib->max;
    int32_t half_neg = center - min;
    int32_t half_pos = max - center;
    int32_t dz_neg = half_neg * (int32_t)deadzone_present / 100;
    int32_t dz_pos = half_pos * (int32_t)deadzone_present / 100;

    //死区内
    if (raw >= (uint16_t)(center - dz_neg) && raw <= (uint16_t)(center + dz_pos)) {
        return JOYSTICK_OUTPUT_MID;
    }

    //死区外负方向
    if (raw < (uint16_t)(center - dz_neg)) {
        int32_t denom = half_neg - dz_neg;
        if(denom <= 0) return JOYSTICK_OUTPUT_MID;
        int32_t out = (int32_t)(raw - min) * JOYSTICK_OUTPUT_MID / denom;
        if(out > JOYSTICK_OUTPUT_MID -1) out = JOYSTICK_OUTPUT_MID -1;
        if(out < 0) out = 0;
        return (uint16_t)out;
    }

    //死区外正方向
    if (raw > (uint16_t)(center + dz_pos)) 
    {
        int32_t denom = half_pos - dz_pos;
        if(denom <= 0) return JOYSTICK_OUTPUT_MID;
        int32_t t = (int32_t)(raw - center - dz_pos) * (JOYSTICK_OUTPUT_MAX - JOYSTICK_OUTPUT_MID) / denom;
        if(t < 0) t = 0;
        if(t > JOYSTICK_OUTPUT_MAX - JOYSTICK_OUTPUT_MID) t = JOYSTICK_OUTPUT_MAX - JOYSTICK_OUTPUT_MID;
        return (uint16_t)(JOYSTICK_OUTPUT_MID + t);
    }

    return JOYSTICK_OUTPUT_MID;
}

//--------------------------------------------------------------------+
// 校准接口
//--------------------------------------------------------------------+

//进入校准模式
void joystick_calib_start(void){
    calib_state = CALIB_IDLE;
}

//采样中心值
void joystick_calib_sample_center(void){
    JoystickData raw;
    uint32_t sum[4] = {0, 0, 0, 0};
    const uint8_t N = 16;

    for(uint8_t i = 0; i < N; i++){
        input_read_joystick(&raw);
        for(uint8_t a = 0; a < 4; a++){
            sum[a] += joystick_read_axis(a, &raw);
        }
    }

    JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
    for(uint8_t a = 0; a < 4; a++){
        calib->calib[a].center = (uint16_t)(sum[a] / N);
    }
    config_mark_dirty(CONFIG_ID_JOYSTICK_CALIB);
    calib_state = CALIB_IDLE;
}

//开始跟踪最大最小值
void joystick_calib_track_start(void){
    JoystickData raw;
    input_read_joystick(&raw);
    for (uint8_t a =0; a < 4; a++) {
        uint16_t v = joystick_read_axis(a, &raw);
        track_min[a] = v;
        track_max[a] = v;
    }
    calib_state = CALIB_TRACKING;
}

//结束跟踪
bool joystick_calib_track_end(void){
    if(calib_state != CALIB_TRACKING) return false;

    JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
    for(uint8_t a = 0; a < 4; a++){
        calib->calib[a].min = track_min[a];
        calib->calib[a].max = track_max[a];
    }
    calib_state = CALIB_IDLE;
    config_mark_dirty(CONFIG_ID_JOYSTICK_CALIB);
    return config_save();
}

//取消校准
void joystick_calib_cancel(void){
    calib_state = CALIB_IDLE;
}


//--------------------------------------------------------------------+
// 主处理
//--------------------------------------------------------------------+

//摇杆初始化
void joystick_init(void){
    calib_state = CALIB_IDLE;
    memset(&joy_out, 0, sizeof(joy_out));
}

//摇杆进程
void joystick_process(void){
    JoystickData raw;
    const JoystickCalibConfig *calib = (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
    const GamepadSettings *settings = (const GamepadSettings *)config_get(CONFIG_ID_GAMEPAD_SETTINGS);
    if(calib == NULL || settings == NULL) return;

    input_read_joystick(&raw);

    for (uint8_t a = 0; a < 4; a++) {
        uint16_t v = joystick_read_axis(a, &raw);
        raw_logical[a] = v;

        //校准跟踪
        if (calib_state == CALIB_TRACKING) {
            if(v < track_min[a]) track_min[a] = v;
            if(v > track_max[a]) track_max[a] = v;
        }

        //死区外满量程映射
        uint16_t out = joystick_map_axis(v, &calib->calib[a], settings->deadzone);
        switch (a) {
            case AXIS_LX: joy_out.lx = out; break;
            case AXIS_LY: joy_out.ly = out; break;
            case AXIS_RX: joy_out.rx = out; break;
            case AXIS_RY: joy_out.ry = out; break;
        }
    }
}

//获取映射后12bit输出
const JoystickOutput* joystick_get_output(void){
    return &joy_out;
}

//获取四轴原始值
const uint16_t* joystick_get_raw(void){
    return raw_logical;
}