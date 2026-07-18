# Config 配置存储系统 — 实现指导

## 概述

借鉴 Betaflight PG（Parameter Group）系统，使用 TLV 格式 + 手动注册方式，实现断电保存配置数据。

## 文件结构

```
Driver/config/
├── config.h           # 核心数据结构、配置组声明、API
├── config.c           # 配置存储逻辑（注册/加载/保存）
├── config_hal.h       # Flash 硬件抽象层接口
├── config_hal.c       # CH32V307 Flash 擦写实现
└── config_crc.h       # CRC16-CCITT 查找表
```

## Flash 布局

CH32V307 的 288K Flash 地址范围：`0x00000000 ~ 0x00047FFF`

```
0x00046000 ┌──────────────┐  扇区 A (4KB) ← 活动扇区
           │  TLV 数据     │
0x00046FFF └──────────────┘
0x00047000 ┌──────────────┐  扇区 B (4KB) ← 备份扇区
           │  TLV 数据     │
0x00047FFF └──────────────┘
```

写入策略：先擦除备用扇区并写入新数据，验证通过后再擦除活动扇区。两个扇区交替使用，断电安全。

## TLV 存储格式

```
┌─────────────────────┐
│ Header (4B)         │  magic(0xCF07) + version(1B) + count(1B)
├─────────────────────┤
│ Record 1 (6+N B)   │  type(2B) + length(2B) + version(1B) + flags(1B) + data(NB)
├─────────────────────┤
│ Record 2 ...        │
├─────────────────────┤
│ Footer (2B)         │  terminator = 0x0000
├─────────────────────┤
│ CRC16 (2B)          │  CRC16-CCITT（header 到 footer 的校验）
└─────────────────────┘
```

---

## 第 1 步：`config_hal.h` — Flash 硬件抽象层接口

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CONFIG_FLASH_SECTOR_SIZE    0x1000  // 4KB
#define CONFIG_FLASH_SECTOR_A_ADDR  0x00046000
#define CONFIG_FLASH_SECTOR_B_ADDR  0x00047000

bool config_hal_erase_sector(uint32_t sector_addr);
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len);
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len);
```

## 第 2 步：`config_hal.c` — Flash 底层实现

```c
#include "config_hal.h"
#include "ch32v30x_flash.h"

bool config_hal_erase_sector(uint32_t sector_addr) {
    FLASH_Unlock();
    FLASH_Status status = FLASH_ErasePage(sector_addr);
    FLASH_Lock();
    return (status == FLASH_COMPLETE);
}

bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    FLASH_Unlock();
    uint32_t i = 0;
    while (i + 1 < len) {
        uint16_t halfword = data[i] | (data[i + 1] << 8);
        if (FLASH_ProgramHalfWord(addr + i, halfword) != FLASH_COMPLETE) {
            FLASH_Lock();
            return false;
        }
        i += 2;
    }
    // 处理奇数字节（补 0xFF）
    if (i < len) {
        FLASH_ProgramHalfWord(addr + i, data[i] | (0xFF << 8));
    }
    FLASH_Lock();
    return true;
}

void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len) {
    const uint8_t *src = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = src[i];
    }
}
```

**注意事项**：
- CH32V307 写 Flash 期间 CPU 无法从 Flash 取指，写入前需关中断（`__disable_irq()`）
- 如果遇到 HardFault，需将 Flash 操作代码放到 RAM 执行（`__attribute__((section(".ramfunc")))`）
- 写入地址必须 2 字节对齐

## 第 3 步：`config_crc.h` — CRC16-CCITT

```c
#pragma once
#include <stdint.h>

static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x54A5,
    0xA54A, 0xB56B, 0x8508, 0x9529, 0xE5CE, 0xF5EF, 0xC58C, 0xD5AD,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x4864, 0x5845, 0x6826, 0x7807, 0x08E0, 0x18C1, 0x28A2, 0x38A3,
    0xC94C, 0xD96D, 0xE90E, 0xF92F, 0x89C8, 0x99E9, 0xA98A, 0xB9AB,
    0x5A55, 0x4A74, 0x7A17, 0x6A36, 0x1AD1, 0x0AF0, 0x3A93, 0x2AB2,
    0xDB5D, 0xCB7C, 0xFB1F, 0xEB3E, 0x9BD9, 0x8BF8, 0xBB9B, 0xABBA,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x85A9, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
};

static inline uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}
```

---

## 第 4 步：`config.h` — 核心头文件

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 配置组编号
// ============================================================
enum {
    CONFIG_ID_JOYSTICK_CALIB   = 1,
    CONFIG_ID_GAMEPAD_SETTINGS = 2,
};

// ============================================================
// 配置组数据结构
// ============================================================

// 摇杆校准数据（24 字节）
typedef struct {
    uint16_t min[4];
    uint16_t max[4];
    uint16_t center[4];
} joystick_calib_t;

// 手柄设置（4 字节）
typedef struct {
    uint16_t deadzone;
    uint8_t  led_mode;
    uint8_t  reserved;
} gamepad_settings_t;

// ============================================================
// 配置组描述符
// ============================================================

typedef struct {
    uint16_t    id;
    uint8_t     version;
    uint8_t     _pad;
    uint16_t    size;
    void       *ram;
    const void *default_data;
    const char *name;
} config_desc_t;

#define CONFIG_MAX_GROUPS 16

// ============================================================
// TLV 格式
// ============================================================

#define CONFIG_MAGIC  0xCF07

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  count;
} __attribute__((packed)) config_header_t;

typedef struct {
    uint16_t type;
    uint16_t length;
    uint8_t  version;
    uint8_t  flags;
    uint8_t  data[];
} __attribute__((packed)) config_record_t;

typedef struct {
    uint16_t terminator;
} __attribute__((packed)) config_footer_t;

// ============================================================
// API
// ============================================================

void        config_init(void);
bool        config_save(void);
bool        config_reset(uint16_t id);
void        config_reset_all(void);
const void* config_get(uint16_t id);
void*       config_get_mutable(uint16_t id);
void        config_mark_dirty(uint16_t id);
uint8_t     config_get_count(void);
const config_desc_t* config_get_desc(uint16_t id);
const config_desc_t* config_get_desc_by_index(uint8_t index);

// ============================================================
// 配置组实例
// ============================================================

extern joystick_calib_t    g_joystick_calib;
extern gamepad_settings_t  g_gamepad_settings;
```

---

## 第 5 步：`config.c` — 配置存储核心

```c
#include "config.h"
#include "config_hal.h"
#include "config_crc.h"
#include <string.h>

// ============================================================
// 配置组 RAM 实例
// ============================================================

joystick_calib_t    g_joystick_calib;
gamepad_settings_t  g_gamepad_settings;

// ============================================================
// 默认值
// ============================================================

static const joystick_calib_t joystick_calib_default = {
    .min    = {0, 0, 0, 0},
    .max    = {4095, 4095, 4095, 4095},
    .center = {2048, 2048, 2048, 2048},
};

static const gamepad_settings_t gamepad_settings_default = {
    .deadzone = 50,
    .led_mode = 0,
};

// ============================================================
// 注册表
// ============================================================

static struct {
    uint8_t       count;
    config_desc_t descs[CONFIG_MAX_GROUPS];
    bool          dirty[CONFIG_MAX_GROUPS];
    uint32_t      active_sector;
} registry;

static void config_register(uint16_t id, uint8_t version, uint16_t size,
                            void *ram, const void *def, const char *name) {
    if (registry.count >= CONFIG_MAX_GROUPS) return;
    config_desc_t *d = &registry.descs[registry.count];
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

// ============================================================
// 查找
// ============================================================

static config_desc_t* find_desc(uint16_t id) {
    for (int i = 0; i < registry.count; i++) {
        if (registry.descs[i].id == id) return &registry.descs[i];
    }
    return NULL;
}

static int find_index(uint16_t id) {
    for (int i = 0; i < registry.count; i++) {
        if (registry.descs[i].id == id) return i;
    }
    return -1;
}

// ============================================================
// 公开 API
// ============================================================

const void* config_get(uint16_t id) {
    config_desc_t *d = find_desc(id);
    return d ? d->ram : NULL;
}

void* config_get_mutable(uint16_t id) {
    config_desc_t *d = find_desc(id);
    return d ? d->ram : NULL;
}

void config_mark_dirty(uint16_t id) {
    int idx = find_index(id);
    if (idx >= 0) registry.dirty[idx] = true;
}

uint8_t config_get_count(void) {
    return registry.count;
}

const config_desc_t* config_get_desc(uint16_t id) {
    return find_desc(id);
}

const config_desc_t* config_get_desc_by_index(uint8_t index) {
    return (index < registry.count) ? &registry.descs[index] : NULL;
}

bool config_reset(uint16_t id) {
    config_desc_t *d = find_desc(id);
    if (!d) return false;
    memcpy(d->ram, d->default_data, d->size);
    registry.dirty[find_index(id)] = true;
    return true;
}

void config_reset_all(void) {
    for (int i = 0; i < registry.count; i++) {
        memcpy(registry.descs[i].ram,
               registry.descs[i].default_data,
               registry.descs[i].size);
        registry.dirty[i] = true;
    }
}

// ============================================================
// Flash 加载
// ============================================================

static bool validate_sector(uint32_t addr) {
    uint8_t buf[CONFIG_FLASH_SECTOR_SIZE];
    config_hal_read(addr, buf, sizeof(buf));
    config_header_t *h = (config_header_t *)buf;
    if (h->magic != CONFIG_MAGIC) return false;
    uint16_t stored = buf[CONFIG_FLASH_SECTOR_SIZE - 2]
                    | (buf[CONFIG_FLASH_SECTOR_SIZE - 1] << 8);
    uint16_t calc = crc16_update(0xFFFF, buf, CONFIG_FLASH_SECTOR_SIZE - 2);
    return (stored == calc);
}

static void load_sector(uint32_t addr) {
    uint8_t buf[CONFIG_FLASH_SECTOR_SIZE];
    config_hal_read(addr, buf, sizeof(buf));
    config_header_t *h = (config_header_t *)buf;
    if (h->magic != CONFIG_MAGIC) return;

    uint32_t offset = sizeof(config_header_t);
    while (offset + sizeof(config_record_t) <= CONFIG_FLASH_SECTOR_SIZE - 4) {
        config_record_t *rec = (config_record_t *)(buf + offset);
        if (rec->type == 0x0000) break;
        if (rec->length < sizeof(config_record_t) ||
            offset + rec->length > CONFIG_FLASH_SECTOR_SIZE - 4) break;

        config_desc_t *d = find_desc(rec->type);
        if (d && rec->version == d->version) {
            uint16_t data_len = rec->length - sizeof(config_record_t);
            uint16_t take = data_len < d->size ? data_len : d->size;
            memcpy(d->ram, rec->data, take);
        }
        offset += rec->length;
    }
}

// ============================================================
// Flash 保存
// ============================================================

bool config_save(void) {
    // 1. 检查脏数据
    bool has_dirty = false;
    for (int i = 0; i < registry.count; i++) {
        if (registry.dirty[i]) { has_dirty = true; break; }
    }
    if (!has_dirty) return true;

    // 2. 选择目标扇区
    uint32_t target = (registry.active_sector == CONFIG_FLASH_SECTOR_A_ADDR)
                    ? CONFIG_FLASH_SECTOR_B_ADDR
                    : CONFIG_FLASH_SECTOR_A_ADDR;

    // 3. 构建 TLV 缓冲区
    uint8_t buf[CONFIG_FLASH_SECTOR_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    uint32_t offset = 0;

    config_header_t header = {
        .magic = CONFIG_MAGIC, .version = 1, .count = registry.count
    };
    memcpy(buf + offset, &header, sizeof(header));
    offset += sizeof(header);

    for (int i = 0; i < registry.count; i++) {
        config_desc_t *d = &registry.descs[i];
        config_record_t rec = {
            .type = d->id,
            .length = sizeof(config_record_t) + d->size,
            .version = d->version,
            .flags = 0,
        };
        if (offset + rec.length > CONFIG_FLASH_SECTOR_SIZE - 4) break;
        memcpy(buf + offset, &rec, sizeof(rec));
        offset += sizeof(rec);
        memcpy(buf + offset, d->ram, d->size);
        offset += d->size;
    }

    config_footer_t footer = { .terminator = 0 };
    memcpy(buf + offset, &footer, sizeof(footer));
    offset += sizeof(footer);

    uint16_t crc = crc16_update(0xFFFF, buf, offset);
    buf[offset]     = crc & 0xFF;
    buf[offset + 1] = crc >> 8;

    // 4. 写入 Flash
    __disable_irq();
    if (!config_hal_erase_sector(target)) { __enable_irq(); return false; }
    if (!config_hal_write(target, buf, sizeof(buf))) { __enable_irq(); return false; }
    __enable_irq();

    // 5. 回读验证
    if (!validate_sector(target)) return false;

    // 6. 更新状态
    registry.active_sector = target;
    for (int i = 0; i < registry.count; i++) registry.dirty[i] = false;
    return true;
}

// ============================================================
// 初始化
// ============================================================

void config_init(void) {
    registry.count = 0;

    config_register(CONFIG_ID_JOYSTICK_CALIB, 1, sizeof(joystick_calib_t),
                    &g_joystick_calib, &joystick_calib_default, "joystick_calib");

    config_register(CONFIG_ID_GAMEPAD_SETTINGS, 1, sizeof(gamepad_settings_t),
                    &g_gamepad_settings, &gamepad_settings_default, "gamepad_settings");

    // 检测活动扇区并加载
    bool a_ok = validate_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    bool b_ok = validate_sector(CONFIG_FLASH_SECTOR_B_ADDR);

    if (a_ok) {
        registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
        load_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    } else if (b_ok) {
        registry.active_sector = CONFIG_FLASH_SECTOR_B_ADDR;
        load_sector(CONFIG_FLASH_SECTOR_B_ADDR);
    } else {
        // 两个扇区都无效，使用默认值
        registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
    }
}
```

---

## 第 6 步：修改 `main.c`

```c
#include "config/config.h"

int main(void)
{
    board_init();
    config_init();         // 加载配置（必须在其他模块初始化之前）
    gamepad_init();
    tud_init(BOARD_TUD_RHPORT);

    printf("SystemClk:%d\r\n", SystemCoreClock);

    while(1)
    {
        tud_task();
        gamepad_process();
        ds4_process(gamepad_get_state());
    }
}
```

---

## 使用示例

### 读取配置

```c
const joystick_calib_t *calib = (const joystick_calib_t *)config_get(CONFIG_ID_JOYSTICK_CALIB);
uint16_t lx_min = calib->min[0];
```

### 修改配置

```c
joystick_calib_t *calib = (joystick_calib_t *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);
calib->min[0] = 100;
calib->max[0] = 4000;
config_mark_dirty(CONFIG_ID_JOYSTICK_CALIB);
```

### 保存到 Flash

```c
config_save();  // 只保存 dirty 的配置组
```

### 重置为默认值

```c
config_reset(CONFIG_ID_JOYSTICK_CALIB);  // 重置单个
config_reset_all();                       // 重置全部
```

---

## 添加新配置组

3 步：

1. **config.h** — 添加结构体和枚举：
```c
enum { ... CONFIG_ID_LED_CONFIG = 3 };
typedef struct { uint8_t brightness, r, g, b; } led_config_t;
extern led_config_t g_led_config;
```

2. **config.c** — 添加实例、默认值、注册：
```c
led_config_t g_led_config;
static const led_config_t led_config_default = {
    .brightness = 128, .r = 0, .g = 0, .b = 255,
};
// 在 config_init() 中：
config_register(CONFIG_ID_LED_CONFIG, 1, sizeof(led_config_t),
                &g_led_config, &led_config_default, "led_config");
```

3. **完成** — 无需修改其他文件，config_save() 会自动包含新配置组。

---

## 调试步骤

1. **测试 Flash 擦写** — 单独调用 `config_hal_erase_sector()` 和 `config_hal_write()`，确认读写正确
2. **测试配置存储** — 修改配置 → `config_save()` → 断电重启 → 检查值是否保持
3. **测试版本不匹配** — 修改配置组版本号，确认旧数据被丢弃、使用默认值
4. **测试双扇区交替** — 连续保存两次，确认两个扇区交替使用
