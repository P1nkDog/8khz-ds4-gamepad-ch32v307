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
};

//--------------------------------------------------------------------+
// 配置组数据结构
//--------------------------------------------------------------------+

//摇杆校准数据
typedef struct{
    JoystickCalibration calib[4]; //[0]=LX [1]=LY [2]=RX [3]=RY
} JoystickCalibConfig;

//手柄设置
//  死区上限：百分比，与 Driver/joystick/joystick.c 的 DEADZONE_PERCENT_MAX 保持一致。
//  该值同时被 joystick_map_axis() 在运行时钳位、被 validate_gamepad_settings() 在写入前拒绝。
#define CONFIG_DEADZONE_MAX 50

typedef struct{
    uint16_t deadzone;
} GamepadSettings;

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


#endif //CONFIG_H