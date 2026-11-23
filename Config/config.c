/********************************** (C) COPYRIGHT  *******************************
* File Name          : config.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/19
* Description        : 手柄配置文件核心逻辑
*********************************************************************************/

#include "config.h"
#include "config_hal.h"
#include "config_crc.h"
#include "ch32v30x.h"
#include <string.h>


//--------------------------------------------------------------------+
// 双层 A/B 配置存储总览
//
//   全局硬件层  ┌ A 0x08036000 / B 0x08037000 ┐
//               │ Group 1  摇杆校准 (24B)      │  与"哪套配置"无关
//               │ Group 3  回报率   (1B)       │  的运行时参数
//               │ Group 13 系统全局 (4B)       │
//               └──────────────────────────────┘
//   独立配置层  ┌ A 0x08038000+idx*0x2000      ┐
//   (idx 0..3)  │ B = A + 0x1000               │  每套一份，切换只是
//               │ Group 14 配置名     (32B)    │  换一对扇区 + 重置
//               │ Group 2  手柄设置   (6B)     │  同一批 RAM
//               │ Group 4  按键映射   (104B)   │
//               │ Group 5~12 宏槽     (8×236B) │  ← 固定尾段
//               └──────────────────────────────┘
//
//   设计要点：
//   1. RAM 只有一份工作集（g_* 全局实例）。切配置 = 把这批变量重置为目标槽位
//      的默认值，再用目标扇区的内容覆盖。因此 config_get(1..14) 返回的指针
//      与 gamepad.c 缓存的 macro_slots[] 永远有效（只换内容，不换地址）。
//   2. 每个 4KB 扇区内部是一份独立的 TLV 流（header + records + footer + CRC16），
//      只序列化"本层"的组；顺序 = 注册顺序（见 config_init）。
//   3. A/B 双扇区：先写 B 并 CRC 校验，再覆盖 A。任意一步掉电都能从另一扇区恢复。
//   4. 与出厂默认逐字节相同的组不落盘（空宏槽零占用）。
//--------------------------------------------------------------------+


//--------------------------------------------------------------------+
// 编译期地址边界校验（五组独立 A/B 扇区，共 10 个 4KB 扇区 = 40KB）
//   - 全部扇区必须落在 [0x08036000, 0x08040000) 之内（256KB 分档也安全）
//   - 每个扇区按 4KB 对齐（FLASH_ROM_ERASE/WRITE 的硬性要求）
//   - 同组内 A 在 B 之前，相邻组"上一组 B → 下一组 A"紧邻，互不重叠
//   - 五组正好铺满 40KB，不越出 0x08040000
//--------------------------------------------------------------------+
_Static_assert(CONFIG_GLOBAL_SECTOR_A_ADDR == 0x08036000U,
               "配置区必须从 Flash 末尾 40KB 的起点 0x08036000 开始");
_Static_assert((CONFIG_GLOBAL_SECTOR_A_ADDR % CONFIG_FLASH_SECTOR_SIZE) == 0,
               "全局层扇区 A 未按 4KB 扇区对齐");
_Static_assert((CONFIG_GLOBAL_SECTOR_B_ADDR % CONFIG_FLASH_SECTOR_SIZE) == 0,
               "全局层扇区 B 未按 4KB 扇区对齐");
_Static_assert(CONFIG_GLOBAL_SECTOR_A_ADDR < CONFIG_GLOBAL_SECTOR_B_ADDR,
               "全局层 A/B 扇区顺序颠倒");
_Static_assert(CONFIG_GLOBAL_SECTOR_B_ADDR < CONFIG_PROFILE_SECTOR_A_ADDR(0),
               "全局层与配置层的扇区发生重叠");
_Static_assert((CONFIG_PROFILE_SECTOR_A_ADDR(0) % CONFIG_FLASH_SECTOR_SIZE) == 0,
               "配置 1 扇区 A 未按 4KB 扇区对齐");
_Static_assert((CONFIG_PROFILE_SECTOR_A_ADDR(1) - CONFIG_PROFILE_SECTOR_B_ADDR(0)) ==
               CONFIG_FLASH_SECTOR_SIZE,
               "配置层扇区不连续（第 0 套 B 与第 1 套 A 之间存在空洞/重叠）");
_Static_assert((CONFIG_PROFILE_SECTOR_A_ADDR(2) - CONFIG_PROFILE_SECTOR_A_ADDR(1)) ==
               CONFIG_PROFILE_SECTOR_STRIDE,
               "配置层扇区跨度与 CONFIG_PROFILE_SECTOR_STRIDE 不一致");
_Static_assert((CONFIG_PROFILE_SECTOR_A_ADDR(3) % CONFIG_FLASH_SECTOR_SIZE) == 0,
               "配置 4 扇区 A 未按 4KB 扇区对齐");
_Static_assert((CONFIG_PROFILE_SECTOR_B_ADDR(CONFIG_PROFILE_COUNT - 1) +
                CONFIG_FLASH_SECTOR_SIZE) == 0x08040000U,
               "五组 A/B 扇区未正好铺满到 0x08040000（越界或未铺满）");

//结构体布局固化：Group 13 固定 4 字节、Group 14 固定 32 字节。
//  两者都会整体 memcpy 进 Flash 并与扇区逐字节比对（空组跳过优化依赖它），
//  长度一旦漂移，旧记录会被按新长度解释。
_Static_assert(sizeof(SystemGlobalConfig) == 4U,
               "SystemGlobalConfig 布局漂移（应为 4 字节：active_profile + shortcut + reserved[2]）");
_Static_assert(sizeof(ProfileMetaConfig) == CONFIG_PROFILE_NAME_MAX,
               "ProfileMetaConfig 布局漂移（应为 32 字节：name[32]）");

//结构体布局固化：2 + 2 + 1 + 1 = 6 字节，且不允许编译器插入隐式填充。
//  该结构体会被整体 memcpy 进 Flash 并与扇区逐字节比对，长度/填充一旦漂移，
//  既会让旧记录被误读，也会让"内容未变"的保存失去跳过擦写的依据。
//  第 6 字节自本版本起由 _reserved 改为 debounce_ms，长度刻意保持不变。
_Static_assert(sizeof(GamepadSettings) == 6,
               "GamepadSettings 布局漂移（应为 6 字节：deadzone_left/right + invert_mask + debounce_ms）");

//按键映射表长度固化：26 个 uint32_t = 104 字节，索引直接就是物理位号 0~25
_Static_assert(sizeof(ButtonMapConfig) == (BUTTON_MAP_COUNT * sizeof(uint32_t)),
               "ButtonMapConfig 布局漂移（应为 104 字节：26 * uint32_t）");

//宏布局固化：单步 14 字节（packed）、整槽 236 字节。
//  236 是协议约定值（网页端按此长度解析/序列化），一旦漂移上位机会整片读错；
//  同时必须 <= MSP_PAYLOAD_MAX - 2 = 252，否则 CONFIG_GET 无法用单帧回传。
_Static_assert(sizeof(MacroStep) == 14U,
               "MacroStep 布局漂移（应为 14 字节：mask u32 + lx/ly/rx/ry i16 + duration_ms u16，必须 packed）");
_Static_assert(sizeof(MacroSlotConfig) == 236U,
               "MacroSlotConfig 布局漂移（应为 236 字节：12 字节头 + 16 × 14 步）");
_Static_assert(sizeof(MacroSlotConfig) <= 252U,
               "MacroSlotConfig 超过单帧 CONFIG_GET 容量（MSP_PAYLOAD_MAX 254 - 头部 2 = 252）");


//--------------------------------------------------------------------+
// 配置组实例
//--------------------------------------------------------------------+
JoystickCalibConfig g_joystickCalib;
GamepadSettings g_gamepadSettings;
PollingRateConfig g_pollingrate;
ButtonMapConfig g_buttonMap;
MacroSlotConfig g_macroSlots[MACRO_SLOT_COUNT];
SystemGlobalConfig g_systemGlobal;   // 全局层：激活配置号 + 盲切总开关
ProfileMetaConfig  g_profileMeta;    // 当前激活配置的自定义名称

//--------------------------------------------------------------------+
// 当前激活配置号（RAM 镜像）
//   g_systemGlobal.active_profile 是唯一权威值，这里只是它的"已钳位"副本：
//   所有扇区地址计算一律用 s_active_profile，避免非法值把读写带到配置区之外。
//--------------------------------------------------------------------+
static uint8_t s_active_profile;

//--------------------------------------------------------------------+
// 静态缓冲区
//   sector_buf 直接作为 FLASH_ROM_WRITE() 的源缓冲区（不再经中间拷贝），
//   所以必须 4 字节对齐；同时扇区大小也必须是 4 的倍数，否则末尾会残留
//   不足一个字的尾部、且校验范围与写入长度会对不上。
//--------------------------------------------------------------------+
_Static_assert((CONFIG_FLASH_SECTOR_SIZE % 4U) == 0U, "sector size must be 4-byte aligned");
static uint8_t sector_buf[CONFIG_FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

//--------------------------------------------------------------------+
// 默认值
//--------------------------------------------------------------------+
static const JoystickCalibConfig joystickCalibDefault = {
    .calib = {
        [0] = { .min = 0, .center = 2048, .max = 4095},
        [1] = { .min = 0, .center = 2048, .max = 4095},
        [2] = { .min = 0, .center = 2048, .max = 4095},
        [3] = { .min = 0, .center = 2048, .max = 4095},
    }
};

static const GamepadSettings gamepadSettingsDefault = {
    .deadzone_left  = 40,
    .deadzone_right = 40,
    .invert_mask    = 0,
    .debounce_ms    = 4,
};

static const PollingRateConfig pollingRateDefault = {
    .level = POLL_RATE_1000,
};

//出厂按键映射：与 TASK-02 之前 gamepad.c 里写死的映射逐项等价。
//  下标即物理位号（见 input.h）：0=UP 1=RIGHT 2=DOWN 3=LEFT
//  4=X(□) 5=A(×) 6=B(○) 7=Y(△) 8=L1 9=R1 10=L2 11=R2
//  12=L3 13=R3 14=PS 15=SELECT 16=START 17=TOUCHPAD；18~25（BTN1~8）默认禁用。
//  注意 4~7 的位序（X/A/B/Y）与 DS4 逻辑枚举（A/B/Y/X）不是同序，逐项对齐过。
const ButtonMapConfig buttonMapDefault = {
    .mask = {
        [0]  = DS4_BTN_BIT(DS4_BTN_UP),        // PE9  网络UP
        [1]  = DS4_BTN_BIT(DS4_BTN_RIGHT),     // PE8  网络RIGHT
        [2]  = DS4_BTN_BIT(DS4_BTN_DOWN),      // PE11 网络DOWN
        [3]  = DS4_BTN_BIT(DS4_BTN_LEFT),      // PE10 网络LEFT
        [4]  = DS4_BTN_BIT(DS4_BTN_X),         // PB11 网络X  → □ Square
        [5]  = DS4_BTN_BIT(DS4_BTN_A),         // PE13 网络A  → × Cross
        [6]  = DS4_BTN_BIT(DS4_BTN_B),         // PE14 网络B  → ○ Circle
        [7]  = DS4_BTN_BIT(DS4_BTN_Y),         // PE15 网络Y  → △ Triangle
        [8]  = DS4_BTN_BIT(DS4_BTN_L1),        // PE7  L1（高电平有效）
        [9]  = DS4_BTN_BIT(DS4_BTN_R1),        // PB10 R1（高电平有效）
        [10] = DS4_BTN_BIT(DS4_BTN_L2),        // PB0  L2
        [11] = DS4_BTN_BIT(DS4_BTN_R2),        // PB1  R2
        [12] = DS4_BTN_BIT(DS4_BTN_L3),        // PA7  左摇杆按下
        [13] = DS4_BTN_BIT(DS4_BTN_R3),        // PE1  右摇杆按下
        [14] = DS4_BTN_BIT(DS4_BTN_PS),        // PE12 PS
        [15] = DS4_BTN_BIT(DS4_BTN_SELECT),    // PB12 SW2（Share）
        [16] = DS4_BTN_BIT(DS4_BTN_START),     // PB14 SW4（Options）
        [17] = DS4_BTN_BIT(DS4_BTN_TOUCHPAD),  // PB13 SW3
        [18] = 0U,                             // PD1  BTN1（禁用）
        [19] = 0U,                             // PD2  BTN2
        [20] = 0U,                             // PD3  BTN3
        [21] = 0U,                             // PD4  BTN4
        [22] = 0U,                             // PD5  BTN5
        [23] = 0U,                             // PD6  BTN6
        [24] = 0U,                             // PD7  BTN7
        [25] = 0U,                             // PB3  BTN8
    }
};

//宏槽位出厂默认：未绑定触发键、0 步、默认打开触发键屏蔽。
//  flags 默认给 BLOCK_TRIGGER，是为了让"用户配好一条宏就生效"时不会出现
//  「宏在跑、触发键同时还在出普通按键」的双触发；不想屏蔽时上位机把它清 0 即可。
const MacroSlotConfig macroSlotDefault = {
    .trigger_mask = 0U,
    .hold_ms      = 0U,
    .mode         = (uint8_t)MACRO_MODE_ONCE,
    .flags        = MACRO_FLAG_BLOCK_TRIGGER,
    .step_count   = 0U,
    .reserved     = { 0U, 0U, 0U },
    // steps 全部 0（未使用步必须全 0，见 macro_slot_validate 的尾段检查）
};

//--------------------------------------------------------------------+
// Group 13 / Group 14 的默认值与名称模板
//--------------------------------------------------------------------+

//全局系统设置出厂默认：激活配置 0 + 盲切开关开启。
//  reserved 全 0 —— 校验回调逐字节检查，非 0 会被拒收。
static const SystemGlobalConfig systemGlobalDefault = {
    .active_profile         = 0U,
    .shortcut_switch_enable = 1U,
    .reserved               = { 0U, 0U },
};

//各槽位的默认配置名（UTF-8，与 "配置 N" 一致）
static const char *const profile_default_names[CONFIG_PROFILE_COUNT] = {
    "配置 1",
    "配置 2",
    "配置 3",
    "配置 4",
};

//Group 14 的"默认值"是**可变**的：内容取决于当前激活槽位（"配置 N"）。
//  注册表里 default_data 指向 profileMetaDefault，因此：
//    - config_reset(14) 的语义 = 恢复成"当前激活槽位的默认名"；
//    - config_save() 的"与默认逐字节相同就跳过落盘"在改名后自然失效（改名必须落盘）；
//    - 切换配置时先按目标槽位刷新本模板，再重置 RAM，两者始终同源。
static ProfileMetaConfig profileMetaDefault;

//按槽位生成默认配置名 "配置 <idx+1>"：先整体清零，再拷名字（保证 '\0' 结尾、余位为 0）
static void fill_default_profile_meta(uint8_t profile_idx, ProfileMetaConfig *out){
    const char *s;
    uint32_t n = 0;

    if (out == NULL) return;

    memset(out, 0, sizeof(*out));

    s = profile_default_names[(uint32_t)profile_idx % CONFIG_PROFILE_COUNT];

    while ((n < (CONFIG_PROFILE_NAME_MAX - 1U)) && (s[n] != '\0')) {
        out->name[n] = s[n];
        n++;
    }
}

//--------------------------------------------------------------------+
// 回报率间隔映射
//--------------------------------------------------------------------+

static const uint32_t polling_interval_us[] = {
    [POLL_RATE_250]  = 4000,   // 250Hz  → 4000μs
    [POLL_RATE_500]  = 2000,   // 500Hz  → 2000μs
    [POLL_RATE_1000] = 1000,   // 1000Hz → 1000μs
    [POLL_RATE_2000] = 500,    // 2000Hz → 500μs
    [POLL_RATE_4000] = 250,    // 4000Hz → 250μs
    [POLL_RATE_8000] = 125,    // 8000Hz → 125μs
};

uint32_t config_get_poll_interval_us(void) {
    const PollingRateConfig *cfg =
        (const PollingRateConfig *)config_get(CONFIG_ID_POLLING_RATE);
    if (cfg == NULL) return polling_interval_us[POLL_RATE_1000];

    const uint32_t cnt = sizeof(polling_interval_us) / sizeof(polling_interval_us[0]);
    if ((uint32_t)cfg->level >= cnt) return polling_interval_us[POLL_RATE_1000];

    return polling_interval_us[cfg->level];
}

//--------------------------------------------------------------------+
// 配置组校验回调
//   共同约定：data 可能指向 MSP 接收缓冲区或 Flash 扇区缓冲（均为非对齐地址），
//   所以一律先 memcpy 到局部的对齐结构体再读成员，绝不直接强转访问
//   uint16_t 成员，规避 RISC-V 非对齐加载异常。
//--------------------------------------------------------------------+

//摇杆校准：每轴必须满足 min < center < max 且 max <= 4095（12bit ADC 满量程）
//  这条约束是 joystick_map_axis() 的前提：
//    half_neg = center - min > 0、half_pos = max - center > 0，
//    denom = half - dz 才会是正数，不会除零、也不会出现负数映射。
static bool validate_joystick_calib(const void *data){
    if (data == NULL) return false;

    JoystickCalibConfig c;
    memcpy(&c, data, sizeof(c));

    for (uint8_t i = 0; i < 4; i++) {
        if (c.calib[i].min    >= c.calib[i].center) return false;
        if (c.calib[i].center >= c.calib[i].max)    return false;
        if (c.calib[i].max    >  4095U)             return false;
    }
    return true;
}

//手柄设置：左右死区计数不得超限、反转掩码不得含非法位、消抖时间不得超上限
static bool validate_gamepad_settings(const void *data){
    if (data == NULL) return false;

    GamepadSettings s;
    memcpy(&s, data, sizeof(s));

    return (s.deadzone_left  <= CONFIG_DEADZONE_MAX) &&
           (s.deadzone_right <= CONFIG_DEADZONE_MAX) &&
           ((s.invert_mask & ~JOYSTICK_INVERT_MASK_ALL) == 0U) &&
           (s.debounce_ms    <= CONFIG_DEBOUNCE_MAX_MS);
}

//回报率：档位必须落在枚举尾部之内，否则 bInterval 计算会越界
static bool validate_polling_rate(const void *data){
    if (data == NULL) return false;

    PollingRateConfig p;
    memcpy(&p, data, sizeof(p));

    return (p.level <= (uint8_t)POLL_RATE_8000);
}

//按键映射：每项只允许 bit1~bit18（DS4_LOGICAL_MASK_ALL）内的位
//  - bit0 是 DS4_BTN_NONE，它不是功能而"禁用"的表示，置位即为非法；
//  - bit19~31 没有任何逻辑功能对应，一旦写进 Flash 也会被 gamepad.c 掩掉，
//    但"存了却静默不生效"比"写入时就报错"更难排查，所以在这里直接拒绝。
static bool validate_button_map(const void *data){
    if (data == NULL) return false;

    ButtonMapConfig m;
    memcpy(&m, data, sizeof(m));

    for (uint8_t i = 0; i < BUTTON_MAP_COUNT; i++) {
        if ((m.mask[i] & ~DS4_LOGICAL_MASK_ALL) != 0U) return false;
    }
    return true;
}

//宏槽位：触发掩码/长按/模式/标志/步数/保留字节全部白名单校验，
//  并且**未使用的步必须全 0**——否则"删了几步"之后残留的旧动作会留在 Flash 里，
//  下次把 step_count 调大时那些幽灵动作会突然复活，现场极难排查。
static bool macro_slot_is_zero(const MacroStep *st){
    return (st->mask == 0U) && (st->lx == 0) && (st->ly == 0) &&
           (st->rx == 0) && (st->ry == 0) && (st->duration_ms == 0U);
}

static bool macro_slot_validate(const void *data){
    if (data == NULL) return false;

    // 先落到局部对齐结构体：data 可能指向 msp_rx_buf（奇地址）或 Flash 镜像，
    // 直接强转读 uint32_t 成员会触发 RISC-V 非对齐访问异常。
    MacroSlotConfig cfg;
    memcpy(&cfg, data, sizeof(cfg));

    if ((cfg.trigger_mask & ~MACRO_TRIGGER_MASK_ALL) != 0U) return false;
    if (cfg.hold_ms > MACRO_HOLD_MS_MAX)                    return false;
    if (cfg.mode > (uint8_t)MACRO_MODE_TOGGLE)              return false;
    if ((cfg.flags & (uint8_t)~MACRO_FLAG_VALID_MASK) != 0U) return false;
    if (cfg.step_count > MACRO_MAX_STEPS)                   return false;
    if ((cfg.reserved[0] | cfg.reserved[1] | cfg.reserved[2]) != 0U) return false;

    for (uint8_t s = 0; s < cfg.step_count; s++) {
        const MacroStep *st = &cfg.steps[s];

        if ((st->mask & ~DS4_LOGICAL_MASK_ALL) != 0U) return false;
        if (st->lx < MACRO_AXIS_OFFSET_MIN || st->lx > MACRO_AXIS_OFFSET_MAX) return false;
        if (st->ly < MACRO_AXIS_OFFSET_MIN || st->ly > MACRO_AXIS_OFFSET_MAX) return false;
        if (st->rx < MACRO_AXIS_OFFSET_MIN || st->rx > MACRO_AXIS_OFFSET_MAX) return false;
        if (st->ry < MACRO_AXIS_OFFSET_MIN || st->ry > MACRO_AXIS_OFFSET_MAX) return false;
        // 时长不能为 0：宏引擎按"到达时长才推进一步"推进，0 会让它原地打转
        if (st->duration_ms < 1U) return false;
    }

    for (uint8_t s = cfg.step_count; s < MACRO_MAX_STEPS; s++) {
        if (!macro_slot_is_zero(&cfg.steps[s])) return false;
    }

    return true;
}

//全局系统设置：激活号必须落在套数之内、开关只允许 0/1、保留字节必须为 0。
//  激活号越界会让 CONFIG_PROFILE_SECTOR_A_ADDR() 算到配置区之外（写到别的
//  Flash 区域），所以这里是硬性边界，必须在写入前拦住。
static bool validate_system_global(const void *data){
    if (data == NULL) return false;

    SystemGlobalConfig c;
    memcpy(&c, data, sizeof(c));

    return (c.active_profile < (uint8_t)CONFIG_PROFILE_COUNT) &&
           (c.shortcut_switch_enable <= 1U) &&
           (c.reserved[0] == 0U) && (c.reserved[1] == 0U);
}

//配置名：非空 + 必须以 '\0' 结尾。
//  name[0] == '\0' 会让上位机显示空名字（分不清"未初始化"和"名字被清空"）；
//  name[31] != '\0' 说明写入方没按 32 字节定长处理，读出来会是越界字符串。
static bool validate_profile_meta(const void *data){
    if (data == NULL) return false;

    ProfileMetaConfig m;
    memcpy(&m, data, sizeof(m));

    return (m.name[0] != '\0') &&
           (m.name[CONFIG_PROFILE_NAME_MAX - 1U] == '\0');
}

//--------------------------------------------------------------------+
// 注册表
//--------------------------------------------------------------------+
static struct {
    uint8_t count; // 已注册数量
    ConfigDesc descs[CONFIG_MAX_GROUPS]; // 描述符数组
    bool dirty[CONFIG_MAX_GROUPS]; // 脏标记（true = 需要保存）
} registry;


// 注册一个配置组
// id: 配置组编号
// version: 版本号（修改结构体时递增）
// layer: 所属层（CONFIG_LAYER_GLOBAL / CONFIG_LAYER_PROFILE）
//        —— 决定该组写进哪一组扇区，也决定同一层内的 TLV 序列化顺序（= 注册顺序）
// size: 数据大小
// ram: RAM 中的变量地址
// def: 默认值地址
// name: 名称（调试用）
// validate: 数值合法性校验回调（NULL = 不做范围校验）
static void config_register(uint16_t id, uint8_t version, ConfigLayer layer,
                            uint16_t size, void *ram, const void *def,
                            const char *name, ConfigValidator validate){
    if (registry.count >= CONFIG_MAX_GROUPS) return;

    ConfigDesc *d = &registry.descs[registry.count];
    d->id = id;
    d->version = version;
    d->layer = (uint8_t)layer;
    d->size = size;
    d->ram = ram;
    d->default_data = def;
    d->name = name;
    d->validate = validate;
    registry.dirty[registry.count] = false;

    memcpy(ram, def, size);

    registry.count++;
}

//--------------------------------------------------------------------+
// 查找
//--------------------------------------------------------------------+

static ConfigDesc* find_desc(uint16_t id){
    for(int i = 0; i < registry.count; i++)
    {  
        if (registry.descs[i].id == id) return &registry.descs[i];
    }
    return NULL;
}

static int find_index(uint16_t id){
    for (int i = 0; i < registry.count; i++) {
        if(registry.descs[i].id == id) return i;
    }
    return -1;
}


//--------------------------------------------------------------------+
// api接口
//--------------------------------------------------------------------+

//重置某个配置为默认值
//  Group 14（配置名）的默认值来自 profileMetaDefault，它始终跟随当前激活槽位，
//  因此这里恢复成的是"当前槽位的默认名（配置 N）"，不会把别的槽位的名字抄过来。
bool config_reset(uint16_t id){
    ConfigDesc *d = find_desc(id);
    if(!d) return false;
    memcpy(d->ram, d->default_data, d->size);
    registry.dirty[find_index(id)] = true;
    return true;
}

//重置所有配置为默认值
//  "所有"指**当前生效的工作集**：独立配置层（14 / 2 / 4 / 5~12）+ 全局层（1 / 3 / 13）。
//  另外三套配置的扇区不会被写坏：config_save() 只落盘当前激活那一套，
//  其余三套的 A/B 扇区保持原样。
void config_reset_all(void){
    for (int i = 0; i < registry.count; i++) {
        memcpy(registry.descs[i].ram,
               registry.descs[i].default_data,
               registry.descs[i].size);
        registry.dirty[i] = true;
    }
}

//获得配置组的只读指针
const void* config_get(uint16_t id){
    ConfigDesc *d = find_desc(id);
    return  d ? d->ram : NULL;
}

//获得配置组的可写指针
void* config_get_mutable(uint16_t id){
    ConfigDesc *d = find_desc(id);
    return  d ? d->ram : NULL;
}

//标记某个配置组已修改
void config_mark_dirty(uint16_t id){
    int idx = find_index(id);
    if (idx >= 0) registry.dirty[idx] = true;
}

//校验某个配置组的数值是否合法
//  data 允许是 MSP 接收缓冲区这类非对齐地址，具体回调内部已做局部拷贝。
bool config_validate(uint16_t id, const void *data){
    if (data == NULL) return false;

    ConfigDesc *d = find_desc(id);
    if (d == NULL) return false;

    // 未挂校验回调 = 该组不做数值范围校验
    if (d->validate == NULL) return true;

    return d->validate(data);
}

//获取已经注册的配置组
uint8_t config_get_count(void){
    return registry.count;
}

//获取配置组描述符
const ConfigDesc* config_get_desc(uint16_t id){
    return find_desc(id);
}

const ConfigDesc* config_get_desc_by_index(uint8_t index){
    return (index < registry.count) ? &registry.descs[index] : NULL;
}


//--------------------------------------------------------------------+
// flash 校验 / 加载
//--------------------------------------------------------------------+

//验证一个扇区的数据是否有效
//  旧实现把整扇区读进全局 sector_buf 再算 CRC，等于"用待写入的数据源当作校验读回
//  缓冲"——正确性完全依赖调用顺序（config_save 里必须"先写后验"），任何人调整顺序
//  都会静默写坏数据。现在改成：头部/尾部各读一个小块，中间按 64B 分块流式累加 CRC，
//  校验过程不碰任何共享缓冲区。
static bool validate_sector(uint32_t addr){
    ConfigHeader h;
    config_hal_read(addr, (uint8_t *)&h, sizeof(h));
    if (h.magic != CONFIG_MAGIC) return false;

    uint8_t tail[2];
    config_hal_read(addr + CONFIG_FLASH_SECTOR_SIZE - 2, tail, sizeof(tail));
    uint16_t stored = (uint16_t)(tail[0] | ((uint16_t)tail[1] << 8));

    uint16_t crc = 0xFFFFu;
    uint8_t chunk[64];
    const uint32_t total = CONFIG_FLASH_SECTOR_SIZE - 2u;

    for (uint32_t off = 0; off < total; off += sizeof(chunk)) {
        uint32_t n = total - off;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        config_hal_read(addr + off, chunk, n);
        crc = crc16_update(crc, chunk, n);
    }

    return (stored == crc);
}

//--------------------------------------------------------------------+
// 层内辅助（按 ConfigDesc.layer 过滤，遍历顺序即注册顺序）
//--------------------------------------------------------------------+

static bool layer_has_dirty(ConfigLayer layer){
    for (uint8_t i = 0; i < registry.count; i++) {
        if ((registry.descs[i].layer == (uint8_t)layer) && registry.dirty[i]) return true;
    }
    return false;
}

static void layer_clear_dirty(ConfigLayer layer){
    for (uint8_t i = 0; i < registry.count; i++) {
        if (registry.descs[i].layer == (uint8_t)layer) registry.dirty[i] = false;
    }
}

//把某一层的所有组恢复成出厂默认（不碰脏标记）
//  切换配置时先用它把工作集擦成目标槽位的默认值，再由 load_layer_from_ab()
//  覆盖扇区里真正存在过的组——扇区里没写过的组（比如空宏槽）就保持默认。
static void layer_apply_defaults(ConfigLayer layer){
    for (uint8_t i = 0; i < registry.count; i++) {
        ConfigDesc *d = &registry.descs[i];
        if ((d->layer == (uint8_t)layer) && (d->default_data != NULL)) {
            memcpy(d->ram, d->default_data, d->size);
        }
    }
}

//--------------------------------------------------------------------+
// 序列化：把一层的组按注册顺序写进单例 sector_buf
//--------------------------------------------------------------------+

static bool build_layer_stream(ConfigLayer layer){
    memset(sector_buf, 0xFF, sizeof(sector_buf));

    uint32_t offset = 0;

    ConfigHeader header = {
        .magic = CONFIG_MAGIC,
        .version = CONFIG_HEADER_VERSION,
        .count = 0,
    };
    memcpy(sector_buf + offset, &header, sizeof(header));
    offset += sizeof(header);

    uint8_t written = 0;

    for (uint8_t i = 0; i < registry.count; i++) {
        ConfigDesc *d = &registry.descs[i];

        //只序列化本层的组：全局扇区不含任何配置层数据，反之亦然
        if (d->layer != (uint8_t)layer) continue;

        //落盘前的最后一道防线：任何组只要当前 RAM 内容不合法，整次保存直接失败。
        //  风险路径：某组正处于"改了一半"的中间态，而保存是按层整体序列化的，
        //  别的组变脏就会把它一并写进 Flash。没有这道守卫，非法内容会带着合法 CRC
        //  进入 Flash；下次启动虽然会被加载时的校验挡下来，代价却是该组被重置为
        //  出厂默认值（用户数据丢失，现场表现为"莫名恢复默认"）。
        if (d->validate != NULL && !d->validate(d->ram)) {
            return false;
        }

        //空组跳过：内容与出厂默认逐字节相同就不落盘。
        //  8 个宏槽位默认值一模一样（未绑定 / 0 步），若原样写入，
        //  236B × 8 + 6B 头 × 8 = 1936 字节，会把 4KB 扇区吃掉近一半，
        //  而"没配过的槽位"开机时已由 config_register() 灌入默认值，
        //  加载时找不到记录同样保持默认——语义完全等价，纯赚 Flash 空间。
        //  宏组位于序列化顺序的尾段，跳过只会在流末尾留下空洞，不会在中间断档。
        if (d->default_data != NULL && memcmp(d->ram, d->default_data, d->size) == 0) {
            continue;
        }

        ConfigRecord rec = {
            .type = d->id,
            .length = (uint16_t)(sizeof(ConfigRecord) + d->size),
            .version = d->version,
            .flags = 0,
        };

        //空间检查：放不下就整体失败返回，绝不静默 break。
        //  -4 是给 footer(2B) 和末尾 CRC(2B) 预留的空间。
        if (offset + rec.length > CONFIG_FLASH_SECTOR_SIZE - 4u) {
            return false;
        }

        memcpy(sector_buf + offset, &rec, sizeof(rec));
        offset += sizeof(rec);
        memcpy(sector_buf + offset, d->ram, d->size);
        offset += d->size;
        written++;
    }

    //回填实际写入的记录数：被跳过的空组不计入 header.count
    header.count = written;
    memcpy(sector_buf, &header, sizeof(header));

    //写 footer（同样要确认放得下，末 2 字节留给 CRC）
    if (offset + sizeof(ConfigFooter) > CONFIG_FLASH_SECTOR_SIZE - 2u) {
        return false;
    }
    ConfigFooter footer = { .terminator = 0 };
    memcpy(sector_buf + offset, &footer, sizeof(footer));
    offset += sizeof(footer);

    //写 CRC（覆盖前 4094 字节）
    uint16_t crc = crc16_update(0xFFFF, sector_buf, CONFIG_FLASH_SECTOR_SIZE - 2u);
    sector_buf[CONFIG_FLASH_SECTOR_SIZE - 2u] = (uint8_t)(crc & 0xFF);
    sector_buf[CONFIG_FLASH_SECTOR_SIZE - 1u] = (uint8_t)(crc >> 8);

    return true;
}

//扇区内容与 sector_buf 是否逐字节一致（用于"无实质变化则跳过擦写"）
static bool sector_equals_buf(uint32_t addr){
    uint8_t chunk[64];

    for (uint32_t off = 0; off < sizeof(sector_buf); off += sizeof(chunk)) {
        config_hal_read(addr + off, chunk, sizeof(chunk));
        if (memcmp(chunk, sector_buf + off, sizeof(chunk)) != 0) {
            return false;
        }
    }
    return true;
}

//从 A/B 双扇区加载一层：A 优先，A 坏则用 B；两者都无效返回 false（RAM 保持原值）
//  只应用"本层"的组：全局扇区里即便混进配置层记录也不会被采纳（反之亦然），
//  这样即使某个扇区被外部工具写脏，也不会把别层的数据串进当前工作集。
static bool load_layer_from_ab(ConfigLayer layer, uint32_t addr_a, uint32_t addr_b){
    uint32_t addr = 0U;

    if (validate_sector(addr_a))      addr = addr_a;
    else if (validate_sector(addr_b)) addr = addr_b;

    if (addr == 0U) return false;

    config_hal_read(addr, sector_buf, sizeof(sector_buf));

    const ConfigHeader *h = (const ConfigHeader *)sector_buf;
    if (h->magic != CONFIG_MAGIC) return false;
    if (h->version != CONFIG_HEADER_VERSION) return false;

    uint32_t offset = sizeof(ConfigHeader);

    while (offset + sizeof(ConfigRecord) <= CONFIG_FLASH_SECTOR_SIZE - 4u) {
        const ConfigRecord *rec = (const ConfigRecord *)(sector_buf + offset);

        //遇到终止符(0x0000)停止
        if (rec->type == 0x0000) break;

        //记录长度异常，停止
        if (rec->length < sizeof(ConfigRecord)) break;
        if (offset + rec->length > CONFIG_FLASH_SECTOR_SIZE - 4u) break;

        ConfigDesc *d = find_desc(rec->type);

        if ((d != NULL) && (d->layer == (uint8_t)layer) && (rec->version == d->version)) {
            uint16_t data_len = (uint16_t)(rec->length - sizeof(ConfigRecord));

            //两道门槛才允许覆盖 RAM：
            //  1) 记录长度必须够（部分拷贝会把结构体填成"前半新、后半旧"的混合态）；
            //  2) 内容必须通过该组的合法性校验（防止 Flash 被写坏 / 老固件留下的
            //     越界值把运行期打挂）。
            // 任一不满足则跳过，RAM 保留调用方预先灌入的默认值。
            if (data_len >= d->size && config_validate(d->id, rec->data)) {
                memcpy(d->ram, rec->data, d->size);
            }
        }
        offset += rec->length;
    }

    return true;
}

//保存一层到 A/B 双扇区：先擦写 B 并 CRC 校验，再擦写 A 并 CRC 校验，成功后清该层脏标记
//  A 是主读扇区，B 是暂存区；任何一步掉电，另一扇区仍保有上一份完整数据。
//  关中断已下沉到 config_hal 层：擦除是一次硬件操作的原子窗口，写入按 256B 硬件页
//  逐页保护、页与页之间放开中断，业务层不再跨"擦除 + 整扇区写入"长时间屏蔽全局中断。
static bool save_layer_to_ab(ConfigLayer layer, uint32_t addr_a, uint32_t addr_b){
    if (!build_layer_stream(layer)) return false;

    //无实质变化则跳过擦写：Flash 擦写寿命量级 1 万次，而"保存"是上位机的高频动作
    //（拖一次滑块就可能触发一次 CONFIG_SAVE）。与主扇区 A 逐字节比对，完全一致就
    //说明这次保存没有任何内容变化，把寿命留给真正需要落盘的修改。
    if (sector_equals_buf(addr_a)) {
        layer_clear_dirty(layer);
        return true;
    }

    //第1步：写 B（暂存）
    if (!config_hal_erase_sector(addr_b)) return false;
    if (!config_hal_write(addr_b, sector_buf, sizeof(sector_buf))) return false;
    if (!validate_sector(addr_b)) return false;

    //第2步：用同一份 buffer 覆盖 A（提交，A 永远是主读扇区）
    if (!config_hal_erase_sector(addr_a)) return false;
    if (!config_hal_write(addr_a, sector_buf, sizeof(sector_buf))) return false;
    if (!validate_sector(addr_a)) return false;

    layer_clear_dirty(layer);
    return true;
}

//在指定扇区里"定点"取 Group 14 的配置名（不占用 sector_buf，不改任何 RAM）
//  记录头按字节手工解析（type/length/version），避免把 Flash 上的非对齐地址
//  强转成 packed 结构体指针；只读记录头，命中目标组才读 32 字节数据。
static bool scan_profile_name(uint32_t addr, char out_name[CONFIG_PROFILE_NAME_MAX]){
    if (out_name == NULL) return false;

    ConfigHeader h;
    config_hal_read(addr, (uint8_t *)&h, sizeof(h));
    if (h.magic != CONFIG_MAGIC) return false;
    if (h.version != CONFIG_HEADER_VERSION) return false;

    uint32_t offset = sizeof(ConfigHeader);

    while (offset + sizeof(ConfigRecord) <= CONFIG_FLASH_SECTOR_SIZE - 4u) {
        uint8_t rh[sizeof(ConfigRecord)];
        config_hal_read(addr + offset, rh, sizeof(rh));

        uint16_t type    = (uint16_t)(rh[0] | ((uint16_t)rh[1] << 8));
        uint16_t length  = (uint16_t)(rh[2] | ((uint16_t)rh[3] << 8));
        uint8_t  version = rh[4];

        if (type == 0x0000) return false;
        if (length < sizeof(ConfigRecord)) return false;
        if (offset + length > CONFIG_FLASH_SECTOR_SIZE - 4u) return false;

        if (type == (uint16_t)CONFIG_GROUP_PROFILE_META) {
            if (version != (uint8_t)CONFIG_PROFILE_META_VERSION) return false;
            if ((uint16_t)(length - sizeof(ConfigRecord)) < CONFIG_PROFILE_NAME_MAX) return false;

            config_hal_read(addr + offset + sizeof(ConfigRecord),
                            (uint8_t *)out_name, CONFIG_PROFILE_NAME_MAX);
            return true;
        }

        offset += length;
    }

    return false;
}


//--------------------------------------------------------------------+
// flash保存
//--------------------------------------------------------------------+

bool config_save(void){
    const bool global_dirty  = layer_has_dirty(CONFIG_LAYER_GLOBAL);
    const bool profile_dirty = layer_has_dirty(CONFIG_LAYER_PROFILE);

    //毫无脏数据：连扇区比对都不做，直接成功返回
    if (!global_dirty && !profile_dirty) return true;

    //顺序：全局层在前。active_profile 属于全局层——先把它落盘，即使紧接着的
    //配置层写入失败，下次启动也只会"打开上一次选中的那套"，不会指向一套
    //从未写过的扇区。
    if (global_dirty) {
        if (!save_layer_to_ab(CONFIG_LAYER_GLOBAL,
                              CONFIG_GLOBAL_SECTOR_A_ADDR,
                              CONFIG_GLOBAL_SECTOR_B_ADDR)) {
            return false;
        }
    }

    if (profile_dirty) {
        const uint8_t idx = config_get_active_profile();

        //只写"当前激活"那一套：其余三套扇区完全不受影响（各套配置互不踩踏）
        if (!save_layer_to_ab(CONFIG_LAYER_PROFILE,
                              CONFIG_PROFILE_SECTOR_A_ADDR(idx),
                              CONFIG_PROFILE_SECTOR_B_ADDR(idx))) {
            return false;
        }
    }

    return true;
}


//--------------------------------------------------------------------+
// 初始化
//--------------------------------------------------------------------+

//出厂默认值自检：逐组用各自的校验回调检验 default_data
//  只在 config_init() 里调一次，成本是一次遍历，换取"默认值永不非法"的保证。
static bool config_validate_defaults(void){
    for (uint8_t i = 0; i < registry.count; i++) {
        const ConfigDesc *d = &registry.descs[i];
        if (d->validate != NULL && !d->validate(d->default_data)) {
            return false;
        }
    }
    return true;
}

void config_init(void){
    registry.count = 0;
    s_active_profile = 0U;

    //组 14 的默认名先填"配置 1"，等全局层读回激活号后再按实际槽位修正
    fill_default_profile_meta(0U, &profileMetaDefault);

    //--------------------------------------------------------------------+
    // 注册顺序 = 同层内的 TLV 序列化顺序，务必保持：
    //   全局层：Group 1 → Group 3 → Group 13
    //   配置层：Group 14 → Group 2 → Group 4 → Group 5 ~ 12（8 组宏固定尾段）
    // 前段刻意紧凑，方便将来在"尾段宏之前"插入新的配置组；宏放最后，是因为
    // 空槽会被跳过，跳过时空洞只出现在流末尾，不会在中间留下断档。
    //--------------------------------------------------------------------+

    //--- 全局硬件层
    config_register(CONFIG_ID_JOYSTICK_CALIB, 1, CONFIG_LAYER_GLOBAL,
                    sizeof(JoystickCalibConfig), &g_joystickCalib,
                    &joystickCalibDefault, "joystick_calib",
                    validate_joystick_calib);
    config_register(CONFIG_ID_POLLING_RATE, 1, CONFIG_LAYER_GLOBAL,
                    sizeof(PollingRateConfig), &g_pollingrate,
                    &pollingRateDefault, "polling_rate",
                    validate_polling_rate);
    config_register(CONFIG_GROUP_SYSTEM_GLOBAL, CONFIG_SYSTEM_GLOBAL_VERSION,
                    CONFIG_LAYER_GLOBAL, sizeof(SystemGlobalConfig),
                    &g_systemGlobal, &systemGlobalDefault, "system_global",
                    validate_system_global);

    //--- 独立配置层（前段）
    //  Group 14 的默认值指向可变的 profileMetaDefault，因此 config_reset(14)
    //  的语义是"恢复成当前激活槽位的默认名（配置 N）"。
    config_register(CONFIG_GROUP_PROFILE_META, CONFIG_PROFILE_META_VERSION,
                    CONFIG_LAYER_PROFILE, sizeof(ProfileMetaConfig),
                    &g_profileMeta, &profileMetaDefault, "profile_meta",
                    validate_profile_meta);
    // 组版本递增到 4：第 6 字节语义由 _reserved(恒 0) 改为 debounce_ms，
    // 旧 Flash 里 version<=3 的记录会在加载处被整条跳过，RAM 回落到新默认值
    //（否则会把旧的 0 误读成"关闭消抖"）。
    config_register(CONFIG_ID_GAMEPAD_SETTINGS, 4, CONFIG_LAYER_PROFILE,
                    sizeof(GamepadSettings), &g_gamepadSettings,
                    &gamepadSettingsDefault, "gamepad_settings",
                    validate_gamepad_settings);
    // 组版本递增到 2：存储由 uint8_t map[26]（26 字节）改为 uint32_t mask[26]
    //（104 字节）。旧记录会在加载处被整条跳过，RAM 回落到出厂默认值。
    config_register(CONFIG_ID_BUTTON_MAP, 2, CONFIG_LAYER_PROFILE,
                    sizeof(ButtonMapConfig), &g_buttonMap,
                    &buttonMapDefault, "button_map",
                    validate_button_map);

    //--- 独立配置层（尾段：8 组宏，id = 5 ~ 12）
    //  8 个槽位共用一个只读默认模板 macroSlotDefault（未绑定 / 0 步），
    //  因此空槽在 build_layer_stream() 里会被整体跳过，不占 Flash。
    for (uint8_t i = 0; i < MACRO_SLOT_COUNT; i++) {
        config_register((uint16_t)(CONFIG_ID_MACRO_BASE + i),
                        CONFIG_MACRO_VERSION, CONFIG_LAYER_PROFILE,
                        sizeof(MacroSlotConfig), &g_macroSlots[i],
                        &macroSlotDefault, "macro_slot",
                        macro_slot_validate);
    }

    // 出厂默认值自检：默认值是所有回退路径的终点，必须 100% 通过自己的校验回调。
    // 不通过说明"校验规则"与"默认常量"不一致，属于开发期缺陷——此时停在死循环里，
    // 比带着非法参数继续跑更容易定位（不引 assert()/abort()，避免把 newlib 的
    // 格式化输出链进来）。
    if (!config_validate_defaults()) {
        for (;;) { __asm volatile ("nop"); }
    }

    //--------------------------------------------------------------------+
    // 分两层加载：先全局层拿到激活号，再只加载当前激活那一套配置
    //--------------------------------------------------------------------+

    //--- 1) 全局硬件层（Group 1 / 3 / 13）：A 优先，A 坏则用 B
    //    两扇区都无效时保持出厂默认（active_profile = 0、盲切开启）
    (void)load_layer_from_ab(CONFIG_LAYER_GLOBAL,
                             CONFIG_GLOBAL_SECTOR_A_ADDR,
                             CONFIG_GLOBAL_SECTOR_B_ADDR);

    //--- 2) 钳位激活号：Flash 被写坏也不能让扇区地址算到配置区之外
    s_active_profile = (g_systemGlobal.active_profile < (uint8_t)CONFIG_PROFILE_COUNT)
                       ? g_systemGlobal.active_profile : 0U;
    g_systemGlobal.active_profile = s_active_profile;

    //--- 3) Group 14 的默认名跟随激活槽位，并把配置层 RAM 整体回落默认值
    fill_default_profile_meta(s_active_profile, &profileMetaDefault);
    layer_apply_defaults(CONFIG_LAYER_PROFILE);

    //--- 4) 独立配置层（Group 14 / 2 / 4 / 5~12）：只加载当前激活那一套
    (void)load_layer_from_ab(CONFIG_LAYER_PROFILE,
                             CONFIG_PROFILE_SECTOR_A_ADDR(s_active_profile),
                             CONFIG_PROFILE_SECTOR_B_ADDR(s_active_profile));

    //--- 5) 启动完成：RAM 与 Flash 已一致，清掉注册期留下的脏标记
    for (int i = 0; i < registry.count; i++) {
        registry.dirty[i] = false;
    }
}


//--------------------------------------------------------------------+
// 多配置层 api
//--------------------------------------------------------------------+

//当前激活配置号（越界一律回报 0）
uint8_t config_get_active_profile(void){
    return (s_active_profile < (uint8_t)CONFIG_PROFILE_COUNT) ? s_active_profile : 0U;
}

//盲切总开关
bool config_is_shortcut_switch_enabled(void){
    return (g_systemGlobal.shortcut_switch_enable != 0U);
}

//切换到指定配置
//  单实例 RAM：重置/加载都发生在同一批变量上，config_get(1..14) 与
//  gamepad.c 缓存的 macro_slots[] 指针全程有效（只换内容，不换地址）。
bool config_switch_profile(uint8_t profile_idx, bool persist_global){
    if (profile_idx >= (uint8_t)CONFIG_PROFILE_COUNT) return false;

    //--- 1) 更新激活号（RAM 权威值 + 全局层可写实例）
    s_active_profile = profile_idx;
    g_systemGlobal.active_profile = profile_idx;

    //--- 2) 独立配置层整体回落为目标槽位的默认值（含 "配置 <idx+1>"）
    fill_default_profile_meta(profile_idx, &profileMetaDefault);
    layer_apply_defaults(CONFIG_LAYER_PROFILE);

    //--- 3) 先清脏标记再加载：目标槽位的内容以 Flash 为准，不存在"待落盘改动"
    layer_clear_dirty(CONFIG_LAYER_PROFILE);

    //--- 4) 从目标槽位的 A/B 扇区加载有效数据（A 优先，A 坏则用 B）
    (void)load_layer_from_ab(CONFIG_LAYER_PROFILE,
                             CONFIG_PROFILE_SECTOR_A_ADDR(profile_idx),
                             CONFIG_PROFILE_SECTOR_B_ADDR(profile_idx));

    //加载只是覆盖 RAM，不应该产生脏标记；再清一次，防止将来改动加载逻辑引入脏标记
    layer_clear_dirty(CONFIG_LAYER_PROFILE);

    //--- 5) 需要时把激活号落盘（只写全局层 A/B，不碰任何配置套扇区）
    if (persist_global) {
        config_mark_dirty(CONFIG_GROUP_SYSTEM_GLOBAL);

        if (!save_layer_to_ab(CONFIG_LAYER_GLOBAL,
                              CONFIG_GLOBAL_SECTOR_A_ADDR,
                              CONFIG_GLOBAL_SECTOR_B_ADDR)) {
            return false;   // 落盘失败：RAM 里已切到新配置，调用方可提示重试
        }
    }

    return true;
}

//读取某个槽位的配置名
void config_get_profile_name(uint8_t profile_idx, char out_name[CONFIG_PROFILE_NAME_MAX]){
    if (out_name == NULL) return;

    memset(out_name, 0, CONFIG_PROFILE_NAME_MAX);

    if (profile_idx >= (uint8_t)CONFIG_PROFILE_COUNT) return;

    //先打默认名 "配置 N" 底稿：后续任何一步取不到合法名字，返回值都是它
    {
        ProfileMetaConfig def;
        fill_default_profile_meta(profile_idx, &def);
        memcpy(out_name, def.name, CONFIG_PROFILE_NAME_MAX);
    }

    //当前激活槽位：直接读 RAM（用户刚改的名字立刻可见）
    if (profile_idx == s_active_profile) {
        if (validate_profile_meta(&g_profileMeta)) {
            memcpy(out_name, g_profileMeta.name, CONFIG_PROFILE_NAME_MAX);
            out_name[CONFIG_PROFILE_NAME_MAX - 1U] = '\0';
        }
        return;
    }

    //其它槽位：轻量扫描该槽位的 A/B 扇区，只取 Group 14 这一条记录。
    //  validate_sector() 分块流式计算 CRC（不碰 sector_buf），scan_profile_name()
    //  只读记录头 + 命中后的 32 字节，全程不改任何 RAM。
    {
        const uint32_t addr_a = CONFIG_PROFILE_SECTOR_A_ADDR(profile_idx);
        const uint32_t addr_b = CONFIG_PROFILE_SECTOR_B_ADDR(profile_idx);
        uint32_t addr = 0U;
        char tmp[CONFIG_PROFILE_NAME_MAX];

        if (validate_sector(addr_a))      addr = addr_a;
        else if (validate_sector(addr_b)) addr = addr_b;

        if (addr == 0U) return;
        if (!scan_profile_name(addr, tmp)) return;

        //名字无效（空 / 末尾没有 '\0'）一律沿用默认名
        if ((tmp[0] == '\0') || (tmp[CONFIG_PROFILE_NAME_MAX - 1U] != '\0')) return;

        memcpy(out_name, tmp, CONFIG_PROFILE_NAME_MAX);
    }
}
