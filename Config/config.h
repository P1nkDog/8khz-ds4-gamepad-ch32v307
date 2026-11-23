/********************************** (C) COPYRIGHT  *******************************
* File Name          : config.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/19
* Description        : 手柄配置文件数据结构，api声明
*********************************************************************************/

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "joystick.h"

//--------------------------------------------------------------------+
// 配置组编号
//--------------------------------------------------------------------+
enum{
    CONFIG_ID_JOYSTICK_CALIB   = 1,
    CONFIG_ID_GAMEPAD_SETTINGS = 2,
    CONFIG_ID_POLLING_RATE     = 3,
    CONFIG_ID_BUTTON_MAP       = 4,
    CONFIG_ID_MACRO_BASE       = 5,   // 宏槽位基址：5 ~ 12，共 8 个槽位
};

//宏槽位编号（CONFIG_ID_MACRO_0 = 5 ~ CONFIG_ID_MACRO_7 = 12）
#define MACRO_SLOT_COUNT    8U
#define CONFIG_ID_MACRO_0   (CONFIG_ID_MACRO_BASE + 0U)
#define CONFIG_ID_MACRO_1   (CONFIG_ID_MACRO_BASE + 1U)
#define CONFIG_ID_MACRO_2   (CONFIG_ID_MACRO_BASE + 2U)
#define CONFIG_ID_MACRO_3   (CONFIG_ID_MACRO_BASE + 3U)
#define CONFIG_ID_MACRO_4   (CONFIG_ID_MACRO_BASE + 4U)
#define CONFIG_ID_MACRO_5   (CONFIG_ID_MACRO_BASE + 5U)
#define CONFIG_ID_MACRO_6   (CONFIG_ID_MACRO_BASE + 6U)
#define CONFIG_ID_MACRO_7   (CONFIG_ID_MACRO_BASE + 7U)

//--------------------------------------------------------------------+
// 配置组数据结构
//--------------------------------------------------------------------+

//摇杆校准数据
typedef struct{
    JoystickCalibration calib[4]; //[0]=LX [1]=LY [2]=RX [3]=RY
} JoystickCalibConfig;

//手柄设置
//  死区上限：12-bit ADC 半量程计数上限（0 ~ 2047），与 Driver/joystick/joystick.c
//  的 JOYSTICK_DEADZONE_MAX 保持一致。
//  该值同时被 joystick_map_axis() 在运行时钳位、被 validate_gamepad_settings() 在写入前拒绝。
#define CONFIG_DEADZONE_MAX 2047U

//--------------------------------------------------------------------+
// 轴向软件反转位掩码（作用于"死区满量程映射之后"的最终输出）
//   与轴编号枚举（0=LX 1=LY 2=RX 3=RY，见 Driver/joystick/joystick.c）按位一一对应，
//   因此 joystick_process() 可以直接用 (1U << axis) 取位。
//   只翻最终结果、不动 raw 与标定坐标系：切换反转无需重新校准摇杆，
//   且中点（JOYSTICK_OUTPUT_MID=2048）反转后严格保持 2048。
//--------------------------------------------------------------------+
#define JOYSTICK_INVERT_LX       (1U << 0)   // 左摇杆 X
#define JOYSTICK_INVERT_LY       (1U << 1)   // 左摇杆 Y
#define JOYSTICK_INVERT_RX       (1U << 2)   // 右摇杆 X
#define JOYSTICK_INVERT_RY       (1U << 3)   // 右摇杆 Y
#define JOYSTICK_INVERT_MASK_ALL 0x0FU       // 合法位全集（bit4~7 必须为 0）

//按键消抖时间上限（毫秒）
//  0 = 关闭消抖（原始电平直通）；Driver/input/input.c 会按此值做运行期钳位。
//  上限取 50ms：远高于机械触点抖动（1~5ms），又不至于让用户感到"按键粘滞"。
#define CONFIG_DEBOUNCE_MAX_MS 50U

typedef struct{
    uint16_t deadzone_left;   // 左摇杆死区 (0 ~ 2047, 12-bit ADC count)
    uint16_t deadzone_right;  // 右摇杆死区 (0 ~ 2047, 12-bit ADC count)
    uint8_t  invert_mask;     // 轴向反转位掩码: bit0=LX, bit1=LY, bit2=RX, bit3=RY
    uint8_t  debounce_ms;     // 按键消抖时间 (0 ~ 50 ms, 0 = 关闭消抖直通)
} GamepadSettings;

//--------------------------------------------------------------------+
// 按键重映射：物理输入位（Driver/input/input.h 的 INPUT_MASK_*）→ DS4 逻辑功能位掩码
//   mask[] 的下标就是物理位号（0~25），值是该物理键要触发的**一组**逻辑功能位。
//   位序必须与 input.h 严格对应：
//     0=UP 1=RIGHT 2=DOWN 3=LEFT 4=X(□) 5=A(×) 6=B(○) 7=Y(△)
//     8=L1 9=R1 10=L2 11=R2 12=L3 13=R3 14=PS 15=SELECT 16=START 17=TOUCHPAD
//     18~25=BTN1~BTN8（扩展键）
//   多对一允许（两个物理键映射到同一功能，按位或叠加）；
//   一对多也允许（一个物理键置多位 → 同时触发多个 DS4 按键）；
//   0 表示禁用该物理键。
//--------------------------------------------------------------------+
#define BUTTON_MAP_COUNT 26U

typedef enum {
    DS4_BTN_NONE     = 0,   // 未映射 / 禁用
    DS4_BTN_UP       = 1,
    DS4_BTN_RIGHT    = 2,
    DS4_BTN_DOWN     = 3,
    DS4_BTN_LEFT     = 4,
    DS4_BTN_A        = 5,   // Cross (×)
    DS4_BTN_B        = 6,   // Circle (○)
    DS4_BTN_Y        = 7,   // Triangle (△)
    DS4_BTN_X        = 8,   // Square (□)
    DS4_BTN_L1       = 9,
    DS4_BTN_R1       = 10,
    DS4_BTN_L2       = 11,
    DS4_BTN_R2       = 12,
    DS4_BTN_L3       = 13,
    DS4_BTN_R3       = 14,
    DS4_BTN_PS       = 15,
    DS4_BTN_SELECT   = 16,  // Share
    DS4_BTN_START    = 17,  // Options
    DS4_BTN_TOUCHPAD = 18,  // Touchpad Click
    DS4_BTN_MAX      = 18
} Ds4LogicalButton;

//逻辑功能位掩码：位号就是 Ds4LogicalButton 的值（1~18）
//  bit0（DS4_BTN_NONE）与 bit19~31 都是非法位：写入前由 validate_button_map() 拒绝，
//  运行期再由 gamepad_build_logical_mask() 用 DS4_LOGICAL_MASK_ALL 掩一次兜底。
#define DS4_BTN_BIT(btn)     (1UL << (uint32_t)(btn))
#define DS4_LOGICAL_MASK_ALL 0x0007FFFEUL   // bit1 ~ bit18 合法

typedef struct {
    // 索引 0~25 对应物理按键 bit0~25，值为逻辑功能位掩码：
    //   0 = 禁用；单个位 = 触发一个功能；多个位 = 一个物理键同时触发多个功能（组合键）
    uint32_t mask[BUTTON_MAP_COUNT];
} ButtonMapConfig;

//--------------------------------------------------------------------+
// 宏引擎配置（8 槽位 × 236 字节）
//   一个槽位 = 触发条件（26 路物理键组合 + 长按阈值）+ 执行模式 + 最多 16 步动作。
//   时间统一以毫秒存储，运行期用 micros() 无符号差值换算成微秒，
//   天然抗 32 位微秒计数器的 71.58 分钟回绕。
//--------------------------------------------------------------------+
#define MACRO_MAX_STEPS      16U
#define MACRO_HOLD_MS_MAX    10000U
#define CONFIG_MACRO_VERSION 1U

//触发掩码：只允许 26 路物理按键（bit0 ~ bit25，与 input.h 的 INPUT_MASK_* 一致）
#define MACRO_TRIGGER_MASK_ALL 0x03FFFFFFUL

//摇杆偏移：相对中点 2048 的有符号偏移，0 = 不干预物理摇杆
#define MACRO_AXIS_OFFSET_MAX  2047
#define MACRO_AXIS_OFFSET_MIN  (-2048)

//执行模式（存为 uint8_t）
typedef enum{
    MACRO_MODE_ONCE   = 0,   // 单次执行：跑完最后一步自动停止
    MACRO_MODE_HOLD   = 1,   // 按住循环：触发键松开立即停止
    MACRO_MODE_TOGGLE = 2,   // 开关切换：再触发一次即关闭
} MacroPlayMode;

//槽位标志位
#define MACRO_FLAG_BLOCK_TRIGGER 0x01U   // 触发期间屏蔽触发键的普通重映射输出
#define MACRO_FLAG_VALID_MASK    0x01U   // 合法标志位全集（其余位必须为 0）

//单步动作
//  **必须 packed**：不加的话 uint32_t 的 4 字节对齐会把 14 撑成 16，
//  16 步就是 256 字节、整槽 268 字节，与 236 字节的协议长度不符。
//  代价是奇偶步的 32 位成员落在非 4 字节对齐地址上，所以访问一律走
//  GCC 为 packed 成员生成的逐字节序列（不会触发 RISC-V 非对齐异常）。
typedef struct{
    uint32_t mask;        // DS4 逻辑按键位掩码 (bit1~bit18, 0 = 本步只做等待/摇杆动作)
    int16_t  lx;          // 左摇杆 X 相对中点偏移 (-2048 ~ +2047, 0 = 不干预物理摇杆)
    int16_t  ly;          // 左摇杆 Y 相对中点偏移 (-2048 ~ +2047, 0 = 不干预物理摇杆)
    int16_t  rx;          // 右摇杆 X 相对中点偏移 (-2048 ~ +2047, 0 = 不干预物理摇杆)
    int16_t  ry;          // 右摇杆 Y 相对中点偏移 (-2048 ~ +2047, 0 = 不干预物理摇杆)
    uint16_t duration_ms; // 本步持续时间 (1 ~ 65535 ms)
} __attribute__((packed)) MacroStep;   // 14 字节

typedef struct{
    uint32_t  trigger_mask; // 触发物理按键组合位掩码 (26 位, 0 = 未绑定/禁用)
    uint16_t  hold_ms;      // 长按触发阈值 (0 ~ 10000 ms, 0 = 按下即触发)
    uint8_t   mode;         // MacroPlayMode (0=ONCE, 1=HOLD, 2=TOGGLE)
    uint8_t   flags;        // 位标志 (bit0: MACRO_FLAG_BLOCK_TRIGGER)
    uint8_t   step_count;   // 有效步数 (0 ~ 16)
    uint8_t   reserved[3];  // 保留对齐字节，必须为 0
    MacroStep steps[MACRO_MAX_STEPS]; // 16 × 14 = 224 字节
} MacroSlotConfig;          // 12 + 224 = 236 字节

//回报率设置
typedef enum{
    POLL_RATE_250  = 0,
    POLL_RATE_500  = 1,
    POLL_RATE_1000 = 2,
    POLL_RATE_2000 = 3,
    POLL_RATE_4000 = 4,
    POLL_RATE_8000 = 5,
} PollingRateLevel;

typedef struct{
    uint8_t level;
} PollingRateConfig;

//--------------------------------------------------------------------+
// 配置组描述符
//--------------------------------------------------------------------+

//配置组数值合法性校验回调
//  data 指向"至少 d->size 字节"的配置组镜像；返回 true 表示数值合法。
//  实现约束：data 可能指向 MSP 接收缓冲区（非对齐），回调内部必须先用
//  局部结构体 memcpy 再访问成员，禁止直接强转读取 uint16_t/uint32_t。
typedef bool (*ConfigValidator)(const void *data);

typedef struct{
    uint16_t id;
    uint8_t  version;
    uint8_t  _pad;
    uint16_t size;
    void     *ram;
    const void *default_data;
    const char *name;
    ConfigValidator validate;   // NULL = 该组不做数值范围校验
} ConfigDesc;

//--------------------------------------------------------------------+
// 配置组容量上限
//   容量核算（受 MSP_CMD_CONFIG_STATUS 单帧载荷反向约束）：
//     - 单帧载荷上限 MSP_PAYLOAD_MAX = 254 字节
//     - 响应首字节是"组数"，之后每组固定占 3 字节
//       （[id:1B][size_L:1B][size_H:1B]）
//     - 理论上限 = (254 - 1) / 3 = 84 组
//   取 64 组：单帧状态查询 (1 + 64*3 = 193 字节) 可一次容纳全部配置组，
//   无需分包；同时给未来收窄载荷或增加字段留出余量。
//   内存代价：每多一组，注册表增加 sizeof(ConfigDesc) + sizeof(bool)
//   （当前 24 + 1 = 25 字节/组）。
//--------------------------------------------------------------------+
#define CONFIG_MAX_GROUPS 64

//--------------------------------------------------------------------+
// tlv数据格式
//--------------------------------------------------------------------+

#define CONFIG_MAGIC 0xCF07

//存储格式版本号（写入 ConfigHeader.version，加载时校验）
//  只有 TLV 布局/字段语义发生不兼容变化时才递增；递增后旧扇区会被整体拒绝，
//  RAM 回落到出厂默认值（属于"宁可重置也不误读"的取舍）。
#define CONFIG_HEADER_VERSION 1

//文件头
typedef struct{
    uint16_t magic;
    uint8_t version;
    uint8_t count;
} __attribute__((packed)) ConfigHeader;

//每个配置组的记录头
typedef struct{
    uint16_t type;
    uint16_t length;
    uint8_t version;
    uint8_t flags;
    uint8_t data[];
} __attribute__((packed)) ConfigRecord;

//文件尾
typedef struct{
    uint16_t terminator;
} __attribute__((packed)) ConfigFooter;

//--------------------------------------------------------------------+
// api接口
//--------------------------------------------------------------------+

//初始化
void config_init(void);

//保存修改
bool config_save(void);

//重置某个配置为默认值
bool config_reset(uint16_t id);

//重置所有配置为默认值
void config_reset_all(void);

//获得配置组的只读指针
const void* config_get(uint16_t id);

//获得配置组的可写指针
void* config_get_mutable(uint16_t id);

//标记某个配置组已修改
void config_mark_dirty(uint16_t id);

//校验某个配置组的数值是否合法
//  data 必须指向"至少该组 size 字节"的配置组镜像（可以是 Flash/接收缓冲区的非对齐地址）。
//  返回 false 表示：data 为空、id 未注册、或该组挂了校验回调且回调判定非法。
//  未挂校验回调的配置组一律返回 true。
bool config_validate(uint16_t id, const void *data);

//获取已经注册的配置组
uint8_t config_get_count(void);

//获取配置组描述符
const ConfigDesc* config_get_desc(uint16_t id);
const ConfigDesc* config_get_desc_by_index(uint8_t index);
uint32_t config_get_poll_interval_us(void);

//--------------------------------------------------------------------+
// 配置组实例
//--------------------------------------------------------------------+
extern JoystickCalibConfig g_joystickCalib;
extern GamepadSettings g_gamepadSettings;
extern PollingRateConfig g_pollingrate;
extern ButtonMapConfig g_buttonMap;
extern MacroSlotConfig g_macroSlots[MACRO_SLOT_COUNT];

//出厂默认映射表：gamepad.c 在配置组不可用时用它回落，保证"配置异常也不会没按键"
extern const ButtonMapConfig buttonMapDefault;

//宏槽位出厂默认值（8 个槽位共享同一份只读模板：未绑定、未启用、0 步）
//  gamepad.c 的宏引擎在 config_get() 返回 NULL 时用它兜底判断"未配置"。
extern const MacroSlotConfig macroSlotDefault;


#endif //CONFIG_H