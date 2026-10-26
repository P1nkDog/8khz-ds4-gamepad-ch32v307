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
// 配置组实例
//--------------------------------------------------------------------+
JoystickCalibConfig g_joystickCalib;
GamepadSettings g_gamepadSettings;
PollingRateConfig g_pollingrate;

//--------------------------------------------------------------------+
// 静态缓冲区
//--------------------------------------------------------------------+

static uint8_t sector_buf[CONFIG_FLASH_SECTOR_SIZE];

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
    .deadzone = 1,
};

static const PollingRateConfig pollingRateDefault = {
    .level = POLL_RATE_1000,
};

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
// 注册表
//--------------------------------------------------------------------+
static struct {
    uint8_t count; // 已注册数量
    ConfigDesc descs[CONFIG_MAX_GROUPS]; // 描述符数组
    bool dirty[CONFIG_MAX_GROUPS]; // 脏标记（true = 需要保存）
    uint32_t active_sector; // 当前活动扇区地址
} registry;


// 注册一个配置组
// id: 配置组编号
// version: 版本号（修改结构体时递增）
// size: 数据大小
// ram: RAM 中的变量地址
// def: 默认值地址
// name: 名称（调试用）
static void config_register(uint16_t id, uint8_t version, uint16_t size,
                            void *ram, const void *def, const char *name){
    if (registry.count >= CONFIG_MAX_GROUPS) return;

    ConfigDesc *d = &registry.descs[registry.count];
    d->id = id;
    d->version = version;
    d->size = size;
    d->ram = ram;
    d->default_data = def;
    d->name = name;
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
bool config_reset(uint16_t id){
    ConfigDesc *d = find_desc(id);
    if(!d) return false;
    memcpy(d->ram, d->default_data, d->size);
    registry.dirty[find_index(id)] = true;
    return true;
}

//重置所有配置为默认值
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
// flash加载
//--------------------------------------------------------------------+

//验证一个扇区的数据是否有效
static bool validate_sector(uint32_t addr){
    config_hal_read(addr, sector_buf, sizeof(sector_buf));

    ConfigHeader *h = (ConfigHeader *)sector_buf;
    if (h->magic != CONFIG_MAGIC) return false;

    uint16_t stored = sector_buf[CONFIG_FLASH_SECTOR_SIZE - 2] | (sector_buf[CONFIG_FLASH_SECTOR_SIZE - 1] <<8);
    uint16_t calc = crc16_update(0xFFFF, sector_buf, CONFIG_FLASH_SECTOR_SIZE - 2);
    return (stored == calc);
}

//从指定扇区加载数据
static void load_sector(uint32_t addr){
    config_hal_read(addr, sector_buf, sizeof(sector_buf));

    ConfigHeader *h = (ConfigHeader *)sector_buf;
    if (h->magic != CONFIG_MAGIC) return;

    //遍历所有tlv数据
    uint32_t offset = sizeof(ConfigHeader);
    while(offset + sizeof(ConfigRecord) <= CONFIG_FLASH_SECTOR_SIZE - 4)
    {
        ConfigRecord *rec = (ConfigRecord *)(sector_buf + offset);

        //遇到终止符(0x0000)停止
        if (rec->type == 0x0000) break;

        //记录长度异常，停止
        if (rec->length < sizeof(ConfigRecord) || offset + rec->length > CONFIG_FLASH_SECTOR_SIZE - 4) break;

        //在配置表中查找匹配的配置组
        ConfigDesc *d = find_desc(rec->type);
        if (d && rec->version == d->version){
            uint16_t data_len = rec->length - sizeof(ConfigRecord);
            uint16_t take = data_len < d->size ? data_len : d->size;
            memcpy(d->ram, rec->data, take);
        }
        offset += rec->length;
    }
}


//--------------------------------------------------------------------+
// flash保存
//--------------------------------------------------------------------+

bool config_save(void){
    //检查是否有脏数据
    bool has_dirty = false;
    for (int i = 0; i < registry.count; i++) {
        if (registry.dirty[i]){
            has_dirty = true;
            break;
        }
    }
    if(!has_dirty) return true;

    
    //在ram中构建完整的tlv数据
    memset(sector_buf, 0xFF, sizeof(sector_buf));
    uint32_t offset = 0;

    //写header
    ConfigHeader header = {
        .magic = CONFIG_MAGIC,
        .version = 1,
        .count = registry.count,
    };
    memcpy(sector_buf + offset, &header, sizeof(header));
    offset += sizeof(header);

    //写每个配置组的record
    for (int i = 0; i < registry.count; i++) {
        ConfigDesc *d = &registry.descs[i];
        ConfigRecord rec = {
            .type = d->id,
            .length = sizeof(ConfigRecord) + d->size,
            .version = d->version,
            .flags = 0,
        };

        //空间检查
        if (offset + rec.length > CONFIG_FLASH_SECTOR_SIZE - 4) break;

        memcpy(sector_buf + offset, &rec, sizeof(rec));
        offset += sizeof(rec);
        memcpy(sector_buf + offset, d->ram, d->size);
        offset += d->size;
    }
    
    //写footer
    ConfigFooter footer = {.terminator = 0};
    memcpy(sector_buf + offset, &footer, sizeof(footer));
    offset += sizeof(footer);

    //写crc
    uint16_t crc = crc16_update(0xFFFF, sector_buf, CONFIG_FLASH_SECTOR_SIZE - 2);
    sector_buf[CONFIG_FLASH_SECTOR_SIZE - 2] = crc & 0xFF;
    sector_buf[CONFIG_FLASH_SECTOR_SIZE - 1] = crc >> 8;

    //【改动】先写暂存扇区 B，校验通过后再覆盖主扇区 A

    //第1步：写 B（暂存）
    __disable_irq();
    if(!config_hal_erase_sector(CONFIG_FLASH_SECTOR_B_ADDR)){
        __enable_irq();
        return false;
    }
    if(!config_hal_write(CONFIG_FLASH_SECTOR_B_ADDR, sector_buf, sizeof(sector_buf))){
        __enable_irq();
        return false;
    }
    __enable_irq();
    if (!validate_sector(CONFIG_FLASH_SECTOR_B_ADDR)) {
        return false;
    }

    //第2步：用同一份 buffer 覆盖 A（提交，A 永远是主读扇区）
    __disable_irq();
    if(!config_hal_erase_sector(CONFIG_FLASH_SECTOR_A_ADDR)){
        __enable_irq();
        return false;
    }
    if(!config_hal_write(CONFIG_FLASH_SECTOR_A_ADDR, sector_buf, sizeof(sector_buf))){
        __enable_irq();
        return false;
    }
    __enable_irq();
    if (!validate_sector(CONFIG_FLASH_SECTOR_A_ADDR)) {
        return false;
    }

    //更新状态：A 始终为主读扇区
    registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
    for(int i = 0; i < registry.count; i++)
    {
        registry.dirty[i] =false; 
    }

    return true;
}


//--------------------------------------------------------------------+
// 初始化
//--------------------------------------------------------------------+

void config_init(void){
    registry.count = 0;

    // 注册所有配置组
    // 参数：编号, 版本, 大小, RAM地址, 默认值地址, 名称
    config_register(CONFIG_ID_JOYSTICK_CALIB, 1, sizeof(JoystickCalibConfig), 
                    &g_joystickCalib, &joystickCalibDefault, "joystick_calib");
    config_register(CONFIG_ID_GAMEPAD_SETTINGS, 1, sizeof(GamepadSettings), 
                    &g_gamepadSettings, &gamepadSettingsDefault, "gamepad_settings");
    config_register(CONFIG_ID_POLLING_RATE, 1, sizeof(PollingRateConfig),
                    &g_pollingrate, &pollingRateDefault, "polling_rate");
    
    //从config加载
    bool a_ok = validate_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    bool b_ok = validate_sector(CONFIG_FLASH_SECTOR_B_ADDR);
   
    if(a_ok){
        registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
        load_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    } else if (b_ok) {
        registry.active_sector = CONFIG_FLASH_SECTOR_B_ADDR;
        load_sector(CONFIG_FLASH_SECTOR_B_ADDR);
    }else {
        registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
    }
}