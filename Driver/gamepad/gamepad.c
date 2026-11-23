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
#include "us_timer.h"
#include <string.h>

static GamepadState state;

//--------------------------------------------------------------------+
// 第一阶段：物理输入位（input.h 的 INPUT_MASK_*）→ DS4 逻辑功能位掩码
//   下标即物理位号 0~25，值取自 ButtonMapConfig.mask[i]（逻辑功能位掩码）。
//   - mask[i] == 0 表示该物理键被禁用，不产生任何逻辑位；
//   - 一对多：mask[i] 置了多位时，按下这一个物理键会同时触发多个 DS4 按键
//     （例如 L1|R1，或 A|X|UP）；
//   - 多对一：两个物理键映射到同一功能时按位或叠加，互不覆盖；
//   - 非法位（bit0 / bit19~31）在配置层已被 validate_button_map() 拒收，
//     这里再用 DS4_LOGICAL_MASK_ALL 掩一次，防止 Flash 被外部工具写坏后
//     触发 (1UL << 32..) 之类的未定义移位；
//   - 上电进入 CDC 配置模式的按键判定在 Driver/usb/usb_descriptors.c 里
//     直接读 PB12 物理电平，不经过本表，因此重映射无法屏蔽配置模式入口。
//--------------------------------------------------------------------+
static uint32_t gamepad_build_logical_mask(uint32_t raw_buttons, const ButtonMapConfig *cfg){
    uint32_t logical = 0;

    for (uint8_t i = 0; i < BUTTON_MAP_COUNT; i++) {
        if ((raw_buttons & (1UL << i)) != 0U) {
            logical |= (cfg->mask[i] & DS4_LOGICAL_MASK_ALL);
        }
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
// 宏引擎（8 槽位）
//   - 触发：物理键组合掩码**完全匹配**（buttons & trigger == trigger）；
//     hold_ms = 0 表示按下即触发，否则需连续按住满 hold_ms 才触发。
//   - 三种模式：ONCE 跑完即停 / HOLD 松开触发键即停 / TOGGLE 再触发一次即停。
//   - 同一时刻只允许一条宏在跑：多个槽位同帧满足条件时，槽号小的优先。
//   - 所有间隔一律用 (uint32_t)(now - t0) 无符号差值。micros() 在 2^32µs
//     （约 71.58 分钟）处回绕，用无符号差值才不会在回绕点误判成"刚超时"或
//     "永远等不到"。
//--------------------------------------------------------------------+
typedef struct{
    uint32_t logical;     // 叠加到逻辑按键掩码上的位
    uint32_t block_mask;  // 从物理按键里屏蔽掉的位（触发键）
    int16_t  axis[4];     // 摇杆偏移（0 = 不干预物理摇杆），顺序 LX/LY/RX/RY
} MacroFrame;

//槽位配置指针：gamepad_init() 里一次性解析，之后 O(1) 取用（注册表注册后不再变化）
static const MacroSlotConfig *macro_slots[MACRO_SLOT_COUNT];

static bool     macro_combo_active[MACRO_SLOT_COUNT];    // 触发组合当前是否按住
static uint32_t macro_press_start_us[MACRO_SLOT_COUNT];  // 本轮按下的起始时刻
static bool     macro_fired_this_hold[MACRO_SLOT_COUNT]; // 本轮按住是否已触发过

static int8_t   macro_active_slot = -1;   // -1 = 空闲
static uint8_t  macro_step_idx;           // 当前步下标
static uint32_t macro_step_start_us;      // 当前步起始时刻（累加式，不随帧抖动漂移）

static const MacroSlotConfig* macro_slot_get(uint8_t slot){
    return (slot < MACRO_SLOT_COUNT) ? macro_slots[slot] : NULL;
}

//触发掩码（永远按 26 位裁剪一次：Flash 被写坏时也不会让屏蔽位溢出到 26 位以外）
static uint32_t macro_trigger_of(const MacroSlotConfig *cfg){
    return (uint32_t)cfg->trigger_mask & (uint32_t)MACRO_TRIGGER_MASK_ALL;
}

//槽位是否可运行：已注册 + 绑了触发键 + 步数在 (0, MACRO_MAX_STEPS] 内
static bool macro_slot_ready(const MacroSlotConfig *cfg){
    return (cfg != NULL) &&
           (macro_trigger_of(cfg) != 0U) &&
           (cfg->step_count > 0U) &&
           (cfg->step_count <= MACRO_MAX_STEPS);
}

static void macro_reset(void){
    macro_active_slot   = -1;
    macro_step_idx      = 0U;
    macro_step_start_us = 0U;
}

static void macro_start(uint8_t slot, uint32_t now_us){
    macro_active_slot   = (int8_t)slot;
    macro_step_idx      = 0U;
    macro_step_start_us = now_us;
}

//摇杆偏移 → 12-bit 输出（2048 + offset，钳位到 0~4095）
static uint16_t macro_axis_output(int16_t offset){
    int32_t v = 2048 + (int32_t)offset;
    if (v < 0)    v = 0;
    if (v > 4095) v = 4095;
    return (uint16_t)v;
}

//推进当前宏的步骤指针，并按"推进后的当前步"填充输出。
//  返回 false 表示宏已经结束（ONCE 跑完 / 配置失效）。
static bool macro_advance(uint32_t now_us, MacroFrame *frame){

    const MacroSlotConfig *cfg = macro_slot_get((uint8_t)macro_active_slot);
    if (!macro_slot_ready(cfg)) {
        macro_reset();
        return false;
    }

    uint8_t guard = 0U;

    for (;;) {
        const MacroStep *step = &cfg->steps[macro_step_idx];
        uint32_t dur_ms = step->duration_ms;

        // 兜底：配置层已拒绝 0 时长，这里再挡一次，避免 while 原地死循环
        if (dur_ms == 0U) dur_ms = 1U;

        if ((uint32_t)(now_us - macro_step_start_us) < (dur_ms * 1000U)) break;

        // 累加步长而不是"以当前时刻重启"：帧间隔抖动不会被逐步累加成整体漂移
        macro_step_start_us += (dur_ms * 1000U);
        macro_step_idx++;

        if (macro_step_idx >= cfg->step_count) {
            if (cfg->mode == (uint8_t)MACRO_MODE_ONCE) {
                macro_reset();
                return false;
            }
            macro_step_idx = 0U;
        }

        // 主循环被长阻塞过（调试器暂停、Flash 擦写）时可能攒下几十次推进；
        // 给个上限，超出就"以当前时刻重新起步"，绝不在 8kHz 热路径里空转。
        if (++guard >= 64U) {
            macro_step_start_us = now_us;
            break;
        }
    }

    const MacroStep *cur = &cfg->steps[macro_step_idx];
    frame->logical = cur->mask & DS4_LOGICAL_MASK_ALL;
    frame->axis[0] = cur->lx;
    frame->axis[1] = cur->ly;
    frame->axis[2] = cur->rx;
    frame->axis[3] = cur->ry;
    return true;
}

//每帧调用：维护触发状态机，产出本帧的宏输出
static void macro_update(uint32_t buttons, MacroFrame *frame){

    const uint32_t now_us = micros();

    frame->logical    = 0U;
    frame->block_mask = 0U;
    frame->axis[0]    = 0;
    frame->axis[1]    = 0;
    frame->axis[2]    = 0;
    frame->axis[3]    = 0;

    //--- 1) 正在运行的宏：配置失效 / HOLD 模式松开触发键 → 立即停止
    if (macro_active_slot >= 0) {
        const MacroSlotConfig *cur = macro_slot_get((uint8_t)macro_active_slot);
        if (!macro_slot_ready(cur)) {
            macro_reset();
        } else if (cur->mode == (uint8_t)MACRO_MODE_HOLD) {
            const uint32_t trig = macro_trigger_of(cur);
            if ((buttons & trig) != trig) macro_reset();
        }
    }

    //--- 2) 逐槽位扫触发（槽号小者优先；同一帧最多启动一条）
    int8_t start_slot = -1;

    for (uint8_t slot = 0; slot < MACRO_SLOT_COUNT; slot++) {
        const MacroSlotConfig *cfg = macro_slot_get(slot);

        if (!macro_slot_ready(cfg)) {
            macro_combo_active[slot]    = false;
            macro_fired_this_hold[slot] = false;
            continue;
        }

        const uint32_t trig = macro_trigger_of(cfg);

        //组合键必须"全部按下"才算触发；松开任意一个即视为松开
        if ((buttons & trig) != trig) {
            macro_combo_active[slot]    = false;
            macro_fired_this_hold[slot] = false;
            continue;
        }

        if (!macro_combo_active[slot]) {
            macro_combo_active[slot]    = true;
            macro_press_start_us[slot]  = now_us;
            macro_fired_this_hold[slot] = false;
        }

        if (macro_fired_this_hold[slot]) continue;

        if ((uint32_t)(now_us - macro_press_start_us[slot]) <
            ((uint32_t)cfg->hold_ms * 1000U)) {
            continue;
        }

        macro_fired_this_hold[slot] = true;

        //TOGGLE：正在跑的这条再被触发一次 = 关闭
        if ((cfg->mode == (uint8_t)MACRO_MODE_TOGGLE) &&
            (macro_active_slot == (int8_t)slot)) {
            macro_reset();
            continue;
        }

        if (start_slot < 0) start_slot = (int8_t)slot;
    }

    if (start_slot >= 0) macro_start((uint8_t)start_slot, now_us);

    //--- 3) 推进当前宏并取当前步的输出
    if (macro_active_slot >= 0) {
        (void)macro_advance(now_us, frame);
    }

    //--- 4) 触发键屏蔽
    //   a) 运行中的宏：无条件屏蔽自己的触发键——这条宏"占有"了这组物理键，
    //      否则会同时输出"宏动作"和"触发键的普通功能"（TOGGLE 模式下尤其明显）；
    //   b) 正在蓄力（已按下但还没满 hold_ms）的槽位：按 MACRO_FLAG_BLOCK_TRIGGER
    //      决定，避免"长按触发"的键在蓄力阶段先漏出一串普通按键。
    uint32_t block = 0U;

    if (macro_active_slot >= 0) {
        const MacroSlotConfig *cur = macro_slot_get((uint8_t)macro_active_slot);
        if (cur != NULL) block |= macro_trigger_of(cur);
    }

    for (uint8_t slot = 0; slot < MACRO_SLOT_COUNT; slot++) {
        if (!macro_combo_active[slot]) continue;

        const MacroSlotConfig *cfg = macro_slot_get(slot);
        if (cfg == NULL) continue;
        if ((cfg->flags & MACRO_FLAG_BLOCK_TRIGGER) == 0U) continue;

        block |= macro_trigger_of(cfg);
    }

    frame->block_mask = block;
}

//--------------------------------------------------------------------+
// 配置盲切（本板无震动马达，切换只靠按键组合 + Flash 记忆）
//
//   Share + △ / □ / ○ / × 长按满 3000ms → 直接切到配置 1 / 2 / 3 / 4。
//   完全受 Group 13 的 shortcut_switch_enable 控制（关闭时本模块不留任何状态）。
//
//   组合键采用**精确匹配**（raw_buttons 必须恰好等于组合值）：这样 "Share + 方向上 + △"
//   这类混合输入不会被误判，也避免用户一边推十字键一边切配置。
//   触发后进入锁定，必须等 Share/△/□/○/× 全部松开才解锁，防止长按在几套配置间来回切。
//
//   防误触：上电时 shortcut_armed = false，必须先观测到一帧"盲切相关键全松开"的
//   空闲状态才开始接受组合键计时——否则"上电即长按 Share+△"会在开机 3 秒后立刻切配置。
//--------------------------------------------------------------------+
#define PROFILE_SHORTCUT_HOLD_US   3000000UL   // 3000ms

#define PROFILE_SHORTCUT_KEYS_MASK (INPUT_MASK_SELECT | INPUT_MASK_BTN_Y | \
                                    INPUT_MASK_BTN_X | INPUT_MASK_BTN_B | INPUT_MASK_BTN_A)

static bool     shortcut_armed     = false;  // 已观测到空闲帧，可以开始计时
static bool     shortcut_locked    = false;  // 已触发，等全部松开
static uint8_t  shortcut_target    = 0xFF;   // 正在计时的目标槽位（0xFF = 无有效组合）
static uint32_t shortcut_start_us  = 0U;     // 组合键按下的起始 micros()

//清空宏引擎运行状态（含各槽位的"本轮已触发/蓄力"标志）
//  切配置后宏槽内容整体换了一套，旧状态必须清零：否则新配置里同名槽位会被残留的
//  combo_active / fired_this_hold 当成"已经触发过"，表现为宏按不出来。
static void macro_stop_all(void){
    for (uint8_t i = 0; i < MACRO_SLOT_COUNT; i++) {
        macro_combo_active[i]    = false;
        macro_press_start_us[i]  = 0U;
        macro_fired_this_hold[i] = false;
    }
    macro_reset();
}

//盲切状态机：每帧调用一次，入参是**消抖后的原始物理按键掩码**
static void profile_shortcut_update(uint32_t raw_buttons){

    //--- 总开关关闭：连状态一起清掉，重新开启后必须重新观测空闲帧
    if (!config_is_shortcut_switch_enabled()) {
        shortcut_armed    = false;
        shortcut_locked   = false;
        shortcut_target   = 0xFF;
        shortcut_start_us = 0U;
        return;
    }

    //--- 释放检查：盲切相关键全松开 → 武装 + 解锁
    //  放在最前面，是为了让"触发后一直按住"的锁定状态能在用户松手的第一帧解除。
    if ((raw_buttons & (uint32_t)PROFILE_SHORTCUT_KEYS_MASK) == 0U) {
        shortcut_armed  = true;
        shortcut_locked = false;
        shortcut_target = 0xFF;
        return;
    }

    //--- 未武装（上电即按住）或已锁定（本轮已切换过）：不计时
    if (!shortcut_armed || shortcut_locked) return;

    //--- 组合键精确匹配（多一个键都不算）
    uint8_t matched = 0xFF;

    if (raw_buttons == (uint32_t)(INPUT_MASK_SELECT | INPUT_MASK_BTN_Y))      matched = 0U;   // Share + △ → 配置 1
    else if (raw_buttons == (uint32_t)(INPUT_MASK_SELECT | INPUT_MASK_BTN_X)) matched = 1U;   // Share + □ → 配置 2
    else if (raw_buttons == (uint32_t)(INPUT_MASK_SELECT | INPUT_MASK_BTN_B)) matched = 2U;   // Share + ○ → 配置 3
    else if (raw_buttons == (uint32_t)(INPUT_MASK_SELECT | INPUT_MASK_BTN_A)) matched = 3U;   // Share + × → 配置 4

    if (matched == 0xFF) {
        //按着 Share 还按了别的键，或在按别的键：本轮计时作废
        shortcut_target = 0xFF;
        return;
    }

    //刚按下或中途换键：重新起计时
    if (matched != shortcut_target) {
        shortcut_target   = matched;
        shortcut_start_us = micros();
        return;
    }

    //微秒无符号差值：32 位计数器 71.58 分钟回绕一次也不会误判成"已满 3 秒"
    if ((uint32_t)(micros() - shortcut_start_us) < PROFILE_SHORTCUT_HOLD_US) return;

    //--- 触发：先锁死本轮，再清宏状态，最后切配置
    shortcut_locked   = true;
    shortcut_target   = 0xFF;
    shortcut_start_us = 0U;

    //宏引擎必须先停：跨配置残留的宏状态会与新槽位的触发条件打架
    macro_stop_all();

    //已经是目标配置就不碰 Flash（避免一次无意义的双扇区擦写）
    if (matched != config_get_active_profile()) {
        (void)config_switch_profile(matched, true);
    }
}

//--------------------------------------------------------------------+
// 公共接口
//--------------------------------------------------------------------+
void gamepad_init(){
    input_init();
    joystick_init();
    memset(&state,0,sizeof(state));
    state.dpad = GAMEPAD_HAT_NOTHING;

    //宏引擎初始化：解析 8 个槽位指针（config_init 已先于本函数执行）+ 清空运行状态。
    //  指针缓存一次即可：配置组注册表在 config_init 之后不再增删，
    //  CONFIG_SET 只改 RAM 内容、不换地址，所以这里存下来的指针始终有效。
    for (uint8_t i = 0; i < MACRO_SLOT_COUNT; i++) {
        macro_slots[i]           = (const MacroSlotConfig *)config_get((uint16_t)(CONFIG_ID_MACRO_BASE + i));
        macro_combo_active[i]    = false;
        macro_press_start_us[i]  = 0U;
        macro_fired_this_hold[i] = false;
    }
    macro_reset();

    //盲切状态机复位：armed = false，上电必须先观测到"盲切相关键全松开"的空闲帧
    shortcut_armed    = false;
    shortcut_locked   = false;
    shortcut_target   = 0xFF;
    shortcut_start_us = 0U;
}

void gamepad_process(){

    uint32_t btn;
    const JoystickOutput *joy;

    joystick_process();
    input_read_buttons(&btn);

    //配置盲切状态机：用**原始物理按键**判断（不经过按键重映射，否则用户把 Share
    //  改成别的功能后就再也切不了配置）。放在宏引擎之前：切换成功后同一帧就能
    //  按新配置的宏表运行，且宏状态已在切换前被清空。
    profile_shortcut_update(btn);

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

    //宏引擎：先跑状态机，拿到本帧的「逻辑位叠加 / 摇杆偏移 / 触发键屏蔽」
    MacroFrame frame;
    macro_update(btn, &frame);

    //普通映射用"扣掉屏蔽位"的物理按键，宏的动作位再按位或叠加：
    //  这样多对一、一对多、宏叠加三种语义可以共存，且互不覆盖。
    uint32_t logical = gamepad_build_logical_mask(btn & ~frame.block_mask, cfg) | frame.logical;
    gamepad_apply_logical(logical);

    //摇杆融合（非侵入）：偏移为 0 的轴一律保留物理摇杆读数
    if (frame.axis[0] != 0) state.lx = macro_axis_output(frame.axis[0]);
    if (frame.axis[1] != 0) state.ly = macro_axis_output(frame.axis[1]);
    if (frame.axis[2] != 0) state.rx = macro_axis_output(frame.axis[2]);
    if (frame.axis[3] != 0) state.ry = macro_axis_output(frame.axis[3]);
}

const GamepadState* gamepad_get_state(){
    return &state;
}
