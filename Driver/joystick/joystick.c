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
#include "input_adc_dma.h"
#include <string.h>

//--------------------------------------------------------------------+
// 常量与轴编号
//--------------------------------------------------------------------+

#define JOYSTICK_OUTPUT_MID 2048
#define JOYSTICK_OUTPUT_MAX 4095
#define JOYSTICK_DEADZONE_MAX 2047U

//死区上限必须与 Config/config.h 的 CONFIG_DEADZONE_MAX 严格一致：
//  JOYSTICK_DEADZONE_MAX 是 joystick_map_axis() 的运行时钳位上限，
//  CONFIG_DEADZONE_MAX   是 config_validate() 的写入前拒绝上限。
//  两个宏一旦分叉，就会出现"校验放行的死区计数被运行层静默钳位"，
//  或者反向"合法值被拒收"，都属于难以排查的隐性不一致，所以在编译期直接绑定。
_Static_assert(CONFIG_DEADZONE_MAX == JOYSTICK_DEADZONE_MAX,
               "deadzone upper bound mismatch between config.h and joystick.c");

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

//单轴死区外满量程映射（12-bit 计数死区：deadzone_counts 即死区半径，单位 ADC count）
//  invert = 后置软件反转：只翻最终映射结果，不参与 raw/标定的坐标系，
//  因此切换反转不会影响已标定的 min/center/max，也无需重新校准。
static uint16_t joystick_map_axis(uint16_t raw, const JoystickCalibration *calib, uint16_t deadzone_counts, bool invert){
    if(deadzone_counts > JOYSTICK_DEADZONE_MAX) deadzone_counts = JOYSTICK_DEADZONE_MAX;

    int32_t center = calib->center;
    int32_t min = calib->min;
    int32_t max = calib->max;
    int32_t half_neg = center - min;
    int32_t half_pos = max - center;
    //死区半径直接取计数值，左右两侧对称
    int32_t dz_neg = (int32_t)deadzone_counts;
    int32_t dz_pos = (int32_t)deadzone_counts;

    //先算出"未反转"的最终映射结果，再统一做后置反转，保证只有一条返回路径
    uint16_t out;

    //以下边界比较全部走 int32_t 有符号运算：
    //  deadzone_counts 可大于 center（例如 center=1200、死区=2047），若沿用
    //  (uint16_t)(center - dz_neg) 强转，负数会被解释成 64xxx 的大正数，
    //  死区窗口整体翻转，映射结果反向。
    if ((int32_t)raw >= (center - dz_neg) && (int32_t)raw <= (center + dz_pos)) {
        //死区内：静止零漂恒为正中
        out = JOYSTICK_OUTPUT_MID;
    } else if ((int32_t)raw < (center - dz_neg)) {
        //死区外负方向
        int32_t denom = half_neg - dz_neg;
        if(denom <= 0){
            out = JOYSTICK_OUTPUT_MID;
        } else {
            int32_t v = ((int32_t)raw - min) * JOYSTICK_OUTPUT_MID / denom;
            if(v > JOYSTICK_OUTPUT_MID -1) v = JOYSTICK_OUTPUT_MID -1;
            if(v < 0) v = 0;
            out = (uint16_t)v;
        }
    } else {
        //死区外正方向
        int32_t denom = half_pos - dz_pos;
        if(denom <= 0){
            out = JOYSTICK_OUTPUT_MID;
        } else {
            int32_t t = ((int32_t)raw - center - dz_pos) * (JOYSTICK_OUTPUT_MAX - JOYSTICK_OUTPUT_MID) / denom;
            if(t < 0) t = 0;
            if(t > JOYSTICK_OUTPUT_MAX - JOYSTICK_OUTPUT_MID) t = JOYSTICK_OUTPUT_MAX - JOYSTICK_OUTPUT_MID;
            out = (uint16_t)(JOYSTICK_OUTPUT_MID + t);
        }
    }

    //后置反转：中点(2048)保持不动，确保死区内静止零漂严格居中；
    //  其余有效位移对称翻转（4095 <-> 0、2048 <-> 2047）。
    if (invert && out != JOYSTICK_OUTPUT_MID) {
        out = (uint16_t)(JOYSTICK_OUTPUT_MAX - out);
    }

    return out;
}

//--------------------------------------------------------------------+
// 校准接口
//--------------------------------------------------------------------+

//进入校准模式
//  只有从 IDLE 进入时才抓快照：连续两次 start 不会用"半成品"覆盖掉原始有效备份。
//  返回值：true = 已进入 CALIB_START；false = 状态不符或配置不可用（调用方据此回报错误）。
bool joystick_calib_start(void){
    if (calib_state != CALIB_IDLE) return false;

    const JoystickCalibConfig *cur =
        (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
    if (cur == NULL) return false;

    memcpy(&calib_backup, cur, sizeof(calib_backup));
    calib_state = CALIB_START;
    return true;
}

//采样中心值（16 帧平均）
//  只接受 CALIB_START / CALIB_CENTER_SAMPLED（后者 = 重采中心）；其余状态直接忽略。
//  返回值：true = 已写入新中心；false = 状态不符或配置不可用。
bool joystick_calib_sample_center(void){
    if (calib_state != CALIB_START && calib_state != CALIB_CENTER_SAMPLED) return false;

    JoystickData raw;
    uint32_t sum[4] = {0, 0, 0, 0};
    const uint8_t N = 16;
    uint32_t seq;

    //摇杆采样改为双 ADC + DMA 后，input_read_joystick() 读的是"最新已完成 Bank"，
    //连续调用会拿到同一份数据，16 帧平均会退化成 1 帧。这里按序号等待新样本：
    //  每次 TIM3 触发写满一个 Bank（2 个 Rank），HT/TC 交替上报，序号 32kHz 递增
    //  ≈ 31.25us 一个新样本，16 帧约 0.5ms；每次等待带 200us 超时兜底（DMA 未启动 /
    //  调试器打断时不会把校准挂死，超时后仍用当前值累加）。
    seq = input_adc_dma_get_sequence();
    for(uint8_t i = 0; i < N; i++){
        seq = input_adc_dma_wait_next(seq, INPUT_ADC_CALIB_WAIT_TIMEOUT_US);
        input_read_joystick(&raw);
        for(uint8_t a = 0; a < 4; a++){
            sum[a] += joystick_read_axis(a, &raw);
        }
    }

    JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
    if (calib == NULL) return false;

    for(uint8_t a = 0; a < 4; a++){
        calib->calib[a].center = (uint16_t)(sum[a] / N);
    }
    //此处刻意不置脏：校准尚未完成，不能被任何一次 config_save()（例如上位机
    //中途发 CONFIG_SAVE）顺带写进 Flash。只有 track_end 校验通过才标记并落盘。
    calib_state = CALIB_CENTER_SAMPLED;
    return true;
}

//开始跟踪最大最小值
//  必须先完成中心采样，否则 min/max 没有基准——直接忽略，防止越阶调用把
//  "未标定"的极值当成有效行程。
//  返回值：true = 已进入 CALIB_TRACKING；false = 状态不符或配置不可用。
bool joystick_calib_track_start(void){
    if (calib_state != CALIB_CENTER_SAMPLED) return false;

    const JoystickCalibConfig *calib =
        (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
    if (calib == NULL) return false;

    for (uint8_t a = 0; a < 4; a++) {
        uint16_t c = calib->calib[a].center;
        track_min[a] = c;
        track_max[a] = c;
        //刻意不改写活动配置的 min/max（只读 center 做跟踪基准）。
        //
        //  旧实现把 min/max 临时收回 center，会让 g_joystickCalib 出现
        //  "min == center == max" 的非法中间态。而 config_save() 是遍历写全部配置组，
        //  只要此刻别的组（死区/回报率）变脏，一次保存就会把这个中间态连同
        //  合法 CRC 一起写进 A/B 两个扇区；下次启动 load_sector() 用
        //  validate_joystick_calib() 判定该记录非法而整条丢弃，用户既有校准被静默
        //  重置为出厂值（数据丢失）。
        //
        //  现在活动配置在校准全程都保持"上一次合法校准"，半成品只存在于
        //  track_min/track_max 这两个纯 RAM 变量里。"跟踪期间不把未标定行程
        //  放大成满量程"这个原始目的，改由 joystick_process() 在 CALIB_TRACKING
        //  下直接输出中点来达成（见该函数）。
    }
    calib_state = CALIB_TRACKING;
    return true;
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
//  返回值：true = 确实做了回滚；false = 本来就在 IDLE（无事可做，上位机可据此判错）。
bool joystick_calib_cancel(void){
    if (calib_state == CALIB_IDLE) return false;

    JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
    if (calib != NULL) {
        memcpy(calib, &calib_backup, sizeof(calib_backup));
    }
    calib_state = CALIB_IDLE;
    return true;
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
        //  跟踪期间不映射，直接输出中点：此时 min/max 尚未标定完，做映射会把
        //  "只推了一半"当成满量程输出；同时活动配置全程保持上一次的合法校准，
        //  不再出现 min==center==max 的非法中间态（见 joystick_calib_track_start）。
        //
        //  CALIB_CENTER_SAMPLED 同样要输出中点：该状态下 center 已被新采样值覆盖，
        //  而 min/max 还是上一次校准的结果。只要新 center 落在旧 [min,max] 之外
        //  （旧校准偏差大时会发生），half_pos 变负 → 死区窗口上界低于 center →
        //  映射退化成"一侧 0..2047、另一侧恒 2048"的半量程中间态。
        //  采样完成到开始跟踪之间的这段窗口，用户在上位机预览里能直接看到轴跳。
        uint16_t out;
        if (calib_state == CALIB_TRACKING || calib_state == CALIB_CENTER_SAMPLED) {
            out = JOYSTICK_OUTPUT_MID;
        } else {
            //左摇杆（LX/LY）用 deadzone_left，右摇杆（RX/RY）用 deadzone_right
            uint16_t dz = (a == AXIS_LX || a == AXIS_LY) ? settings->deadzone_left
                                                         : settings->deadzone_right;
            //轴向反转位：轴编号枚举 0=LX 1=LY 2=RX 3=RY 与掩码位序一一对应
            bool invert = (settings->invert_mask & (uint8_t)(1U << a)) != 0U;
            out = joystick_map_axis(v, &calib->calib[a], dz, invert);
        }
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