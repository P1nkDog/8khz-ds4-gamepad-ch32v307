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

//校准状态流转（严格单向，任何越阶调用都被忽略）：
//
//   CALIB_IDLE ──start──▶ CALIB_START ──sample──▶ CALIB_CENTER_SAMPLED
//        ▲                    │                        │
//        │                    │ cancel                 │ track_start
//        │                    ▼                        ▼
//        └────── cancel/回滚 ─────────────────── CALIB_TRACKING ──end──▶ CALIB_IDLE
//
// 只有 CALIB_TRACKING 才允许写 Flash；其余状态中断一律回到 IDLE + 快照回滚。
typedef enum{
    CALIB_IDLE = 0,          //未校准
    CALIB_START,             //已进入校准（快照已备份，等待采样中心）
    CALIB_CENTER_SAMPLED,    //中心已采样（允许重采，等待开始跟踪）
    CALIB_TRACKING,          //正在跟踪各轴极值
} CalibState;

//单侧有效行程下限（ADC count）
//  track_end 判"是否推到位"用：只判 min<center<max 挡不住噪声——静止时也要有
//  几个 count 的抖动，那样噪声就能"校准"成功并把行程放大成满量程。
//  200 count 约占 12bit 全量程的 4.9%，远低于真实摇杆的机械行程，不会误杀正常校准。
#define CALIB_MIN_SPAN 200


//--------------------------------------------------------------------+
// 静态变量
//--------------------------------------------------------------------+

static CalibState calib_state;
static JoystickOutput joy_out;
static uint16_t raw_logical[4]; //4轴逻辑原始值（取反后）
static uint16_t track_min[4];
static uint16_t track_max[4];
static JoystickCalibConfig calib_backup; //校准前快照，用于 cancel/失败回滚


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
//  只有从 IDLE 进入时才抓快照：连续两次 start 不会用"半成品"覆盖掉原始有效备份。
void joystick_calib_start(void){
    if (calib_state != CALIB_IDLE) return;

    const JoystickCalibConfig *cur =
        (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
    if (cur == NULL) return;

    memcpy(&calib_backup, cur, sizeof(calib_backup));
    calib_state = CALIB_START;
}

//采样中心值（16 帧平均）
//  只接受 CALIB_START / CALIB_CENTER_SAMPLED（后者 = 重采中心）；其余状态直接忽略。
void joystick_calib_sample_center(void){
    if (calib_state != CALIB_START && calib_state != CALIB_CENTER_SAMPLED) return;

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
    if (calib == NULL) return;

    for(uint8_t a = 0; a < 4; a++){
        calib->calib[a].center = (uint16_t)(sum[a] / N);
    }
    //此处刻意不置脏：校准尚未完成，不能被任何一次 config_save()（例如上位机
    //中途发 CONFIG_SAVE）顺带写进 Flash。只有 track_end 校验通过才标记并落盘。
    calib_state = CALIB_CENTER_SAMPLED;
}

//开始跟踪最大最小值
//  必须先完成中心采样，否则 min/max 没有基准——直接忽略，防止越阶调用把
//  "未标定"的极值当成有效行程。
void joystick_calib_track_start(void){
    if (calib_state != CALIB_CENTER_SAMPLED) return;

    JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
    if (calib == NULL) return;

    for (uint8_t a = 0; a < 4; a++) {
        uint16_t c = calib->calib[a].center;
        track_min[a] = c;
        track_max[a] = c;
        //跟踪期间把 min/max 收回 center：摇杆输出恒定中点，既不会把还没标定的
        //行程放大成满量程，也避免 center/min/max 三者关系暂时失效导致映射异常。
        calib->calib[a].min = c;
        calib->calib[a].max = c;
    }
    calib_state = CALIB_TRACKING;
}

//跟踪阶段单轴极值更新（只由 joystick_process 每帧调用）
static void joystick_calib_update(uint8_t axis, uint16_t v){
    if (calib_state != CALIB_TRACKING) return;
    if (axis >= 4) return;

    if (v < track_min[axis]) track_min[axis] = v;
    if (v > track_max[axis]) track_max[axis] = v;
}

//结束跟踪
//  三道关：状态必须是 TRACKING、每侧行程都要够（CALIB_MIN_SPAN）、整组要通过
//  config_validate()。任一不过 → 用快照回滚、回到 IDLE、返回 false，绝不落盘。
bool joystick_calib_track_end(void){
    if(calib_state != CALIB_TRACKING) return false;

    JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
    if (calib == NULL) {
        calib_state = CALIB_IDLE;
        return false;
    }

    //先把跟踪到的极值写入活动配置（MSP 的 CALIB_END 会紧接着读回去回报）
    for(uint8_t a = 0; a < 4; a++){
        calib->calib[a].min = track_min[a];
        calib->calib[a].max = track_max[a];
    }

    //行程检查：min < center < max 是基本结构，另加每侧最小跨度挡住"没推动"
    bool ok = true;
    for (uint8_t a = 0; a < 4; a++) {
        const JoystickCalibration *c = &calib->calib[a];
        if (c->min >= c->center || c->center >= c->max) { ok = false; break; }
        if ((uint16_t)(c->center - c->min) < CALIB_MIN_SPAN) { ok = false; break; }
        if ((uint16_t)(c->max - c->center) < CALIB_MIN_SPAN) { ok = false; break; }
    }

    //最终合法性：走配置层统一的校验回调（min < center < max <= 4095）
    if (ok && !config_validate(CONFIG_ID_JOYSTICK_CALIB, &g_joystickCalib)) {
        ok = false;
    }

    if (!ok) {
        memcpy(calib, &calib_backup, sizeof(calib_backup)); //回滚半成品
        calib_state = CALIB_IDLE;
        return false;
    }

    calib_state = CALIB_IDLE;
    config_mark_dirty(CONFIG_ID_JOYSTICK_CALIB);
    //落盘失败时 RAM 保留这份合法校准、dirty 仍为真，上位机可重发 CONFIG_SAVE 重试
    return config_save();
}

//取消校准
//  处于任意非 IDLE 状态都回滚到校准前快照——包括"已采样中心但还没跟踪"这种
//  半成品，避免新中心留在 RAM 里污染后续映射。
void joystick_calib_cancel(void){
    if (calib_state == CALIB_IDLE) return;

    JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
    if (calib != NULL) {
        memcpy(calib, &calib_backup, sizeof(calib_backup));
    }
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

        //校准跟踪：仅在 CALIB_TRACKING 下累积极值，其余状态是空操作
        joystick_calib_update(a, v);

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