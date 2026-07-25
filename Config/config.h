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

typedef struct{
    uint16_t id;
    uint8_t  version;
    uint8_t  _pad;
    uint16_t size;
    void     *ram;
    const void *default_data;
    const char *name;
} ConfigDesc;

#define CONFIG_MAX_GROUPS 16

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