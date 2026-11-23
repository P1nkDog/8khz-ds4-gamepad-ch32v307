/********************************** (C) COPYRIGHT *******************************
* File Name          : gamepad.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/10
* Description        : GPIO按键和摇杆ADC数据映射（物理位 → 逻辑功能 → DS4 报告）
*********************************************************************************/

#include "gamepad.h"
#include "input.h"
#include "joystick.h"
#include "config.h"
#include <string.h>

static GamepadState state;

//--------------------------------------------------------------------+
// 第一阶段：物理输入位（input.h 的 INPUT_MASK_*）→ DS4 逻辑功能位掩码
//   下标即物理位号 0~25，值取自 ButtonMapConfig.map[i]（Ds4LogicalButton）。
//   - 0（DS4_BTN_NONE）表示该物理键被禁用，不产生任何逻辑位；
//   - 多对一允许：两个物理键映射到同一功能时按位或叠加，互不覆盖；
//   - 越界值（> DS4_BTN_MAX）在配置层已被 validate_button_map() 拒收，
//     这里再挡一次，避免 (1UL << 32..) 触发未定义移位。
//   - 上电进入 CDC 配置模式的按键判定在 Driver/usb/usb_descriptors.c 里
//     直接读 PB12 物理电平，不经过本表，因此重映射无法屏蔽配置模式入口。
//--------------------------------------------------------------------+
static uint32_t gamepad_build_logical_mask(uint32_t raw_buttons, const ButtonMapConfig *cfg){
    uint32_t logical = 0;

    for (uint8_t i = 0; i < BUTTON_MAP_COUNT; i++) {
        if ((raw_buttons & (1UL << i)) == 0U) continue;

        const uint8_t target = cfg->map[i];
        if (target == (uint8_t)DS4_BTN_NONE || target > (uint8_t)DS4_BTN_MAX) continue;

        logical |= (1UL << target);
    }

    return logical;
}

//逻辑功能位测试
#define LOGICAL_SET(mask, btn)  (((mask) & (1UL << (btn))) != 0U)

//--------------------------------------------------------------------+
// 第二阶段：逻辑位 → 十字键 Hat Switch
//   方向组合与优先级沿用重映射之前的实现（斜向优先，其次单方向互斥）
//--------------------------------------------------------------------+
static uint8_t gamepad_dpad_map(uint32_t logical){

    uint8_t up    = LOGICAL_SET(logical, DS4_BTN_UP)    ? 1 : 0;
    uint8_t right = LOGICAL_SET(logical, DS4_BTN_RIGHT) ? 1 : 0;
    uint8_t down  = LOGICAL_SET(logical, DS4_BTN_DOWN)  ? 1 : 0;
    uint8_t left  = LOGICAL_SET(logical, DS4_BTN_LEFT)  ? 1 : 0;

    if (up && right) return GAMEPAD_HAT_UPRIGHT;
    if (down && right) return GAMEPAD_HAT_DOWNRIGHT;
    if (down && left) return GAMEPAD_HAT_DOWNLEFT;
    if (up && left) return GAMEPAD_HAT_UPLEFT ;

    if (up) return GAMEPAD_HAT_UP;
    if (right) return GAMEPAD_HAT_RIGHT;
    if (down) return GAMEPAD_HAT_DOWN ;
    if (left) return GAMEPAD_HAT_LEFT;

    return GAMEPAD_HAT_NOTHING; 
}

//--------------------------------------------------------------------+
// 第二阶段：逻辑位 → GamepadState 各字段
//   字段名沿用 DS4 报告的方位命名（West=□ / South=× / East=○ / North=△）
//--------------------------------------------------------------------+
static void gamepad_apply_logical(uint32_t logical){

    state.dpad = gamepad_dpad_map(logical);

    state.buttonWest         = LOGICAL_SET(logical, DS4_BTN_X)        ? 1 : 0;   // □ Square
    state.buttonSouth        = LOGICAL_SET(logical, DS4_BTN_A)        ? 1 : 0;   // × Cross
    state.buttonEast         = LOGICAL_SET(logical, DS4_BTN_B)        ? 1 : 0;   // ○ Circle
    state.buttonNorth        = LOGICAL_SET(logical, DS4_BTN_Y)        ? 1 : 0;   // △ Triangle
    state.buttonL1           = LOGICAL_SET(logical, DS4_BTN_L1)       ? 1 : 0;
    state.buttonR1           = LOGICAL_SET(logical, DS4_BTN_R1)       ? 1 : 0;
    state.buttonL2           = LOGICAL_SET(logical, DS4_BTN_L2)       ? 1 : 0;
    state.buttonR2           = LOGICAL_SET(logical, DS4_BTN_R2)       ? 1 : 0;
    state.buttonSelect       = LOGICAL_SET(logical, DS4_BTN_SELECT)   ? 1 : 0;   // Share
    state.buttonStart        = LOGICAL_SET(logical, DS4_BTN_START)    ? 1 : 0;   // Options
    state.buttonL3           = LOGICAL_SET(logical, DS4_BTN_L3)       ? 1 : 0;
    state.buttonR3           = LOGICAL_SET(logical, DS4_BTN_R3)       ? 1 : 0;
    state.buttonHome         = LOGICAL_SET(logical, DS4_BTN_PS)       ? 1 : 0;
    state.buttonTouchpad     = LOGICAL_SET(logical, DS4_BTN_TOUCHPAD) ? 1 : 0;
}

//--------------------------------------------------------------------+
// 公共接口
//--------------------------------------------------------------------+
void gamepad_init(){
    input_init();
    joystick_init();
    memset(&state,0,sizeof(state));
    state.dpad = GAMEPAD_HAT_NOTHING;
}

void gamepad_process(){

    uint32_t btn;
    const JoystickOutput *joy;

    joystick_process();
    input_read_buttons(&btn);

    joy = joystick_get_output();
    state.lx = joy->lx;
    state.ly = joy->ly;
    state.rx = joy->rx;
    state.ry = joy->ry;

    //配置组缺失（未注册 / 初始化异常）时回落到出厂映射表：
    //  宁可让按键按出厂功能走，也不要出现"整机没按键"。
    const ButtonMapConfig *cfg =
        (const ButtonMapConfig *)config_get(CONFIG_ID_BUTTON_MAP);
    if (cfg == NULL) cfg = &buttonMapDefault;

    gamepad_apply_logical(gamepad_build_logical_mask(btn, cfg));
}

const GamepadState* gamepad_get_state(){
    return &state;
}
