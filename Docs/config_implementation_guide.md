# Config 配置存储 + WebSerial 命令系统 — 完整实现指导

## 概述

两个功能模块：
1. **Config 配置存储** — 借鉴 Betaflight PG，TLV 格式 + 手动注册，断电保存
2. **WebSerial 命令接口** — USB CDC 虚拟串口 + 命令解析器，支持网页修改配置

## 文件结构

```
Driver/config/
├── config.h           # 核心数据结构、配置组声明、API
├── config.c           # 配置存储逻辑（注册/加载/保存）
├── config_hal.h       # Flash 硬件抽象层接口
├── config_hal.c       # CH32V307 Flash 擦写实现
├── config_crc.h       # CRC16-CCITT 查找表
├── cmd_parser.h       # 命令解析器接口
├── cmd_parser.c       # 命令解析器实现
└── cmd_web.h          # 网页文件（HTML/JS 内嵌为 C 字符串）
```

## USB 复合设备端点分配

| 资源 | 地址 | 类型 | 说明 |
|------|------|------|------|
| HID EP IN | 0x81 | Interrupt | 手柄数据上报 |
| HID EP OUT | 0x02 | Interrupt | 手柄接收（原 0x03 改为 0x02） |
| CDC Notify | 0x83 | Interrupt IN | CDC 通知 |
| CDC Data OUT | 0x04 | Bulk | CDC 数据接收 |
| CDC Data IN | 0x84 | Bulk | CDC 数据发送 |

接口：HID=0, CDC_Comm=1, CDC_Data=2, 共 3 个接口

---

## 第 1 步：`config_hal.h` — Flash 硬件抽象层

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
- CH32V307 写 Flash 期间 CPU 无法从 Flash 取指，建议写入前关中断
- 如果遇到 HardFault，需要将 Flash 操作代码放到 RAM 执行（`__attribute__((section(".ramfunc")))`）

## 第 3 步：`config_crc.h` — CRC16

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

// 摇杆校准数据（12 x uint16 = 24 字节）
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
// 配置组描述符（内部使用）
// ============================================================

typedef struct {
    uint16_t    id;            // 配置组编号
    uint8_t     version;       // 版本号
    uint8_t     _pad;
    uint16_t    size;          // 数据大小
    void       *ram;           // RAM 中的数据地址
    const void *default_data;  // 默认值地址
    const char *name;          // 名称（用于命令行）
    const char *fields;        // 字段描述（用于 JSON 解析）
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
// 配置组实例（外部可访问）
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
#include <stdio.h>

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
    uint8_t      count;
    config_desc_t descs[CONFIG_MAX_GROUPS];
    bool         dirty[CONFIG_MAX_GROUPS];
    uint32_t     active_sector;
} registry;

static void config_register(uint16_t id, uint8_t version, uint16_t size,
                            void *ram, const void *def,
                            const char *name, const char *fields) {
    if (registry.count >= CONFIG_MAX_GROUPS) return;
    config_desc_t *d = &registry.descs[registry.count];
    d->id = id;
    d->version = version;
    d->size = size;
    d->ram = ram;
    d->default_data = def;
    d->name = name;
    d->fields = fields;
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

uint8_t config_get_count(void) { return registry.count; }

const config_desc_t* config_get_desc(uint16_t id) { return find_desc(id); }

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
        memcpy(registry.descs[i].ram, registry.descs[i].default_data, registry.descs[i].size);
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
    bool has_dirty = false;
    for (int i = 0; i < registry.count; i++) {
        if (registry.dirty[i]) { has_dirty = true; break; }
    }
    if (!has_dirty) return true;

    uint32_t target = (registry.active_sector == CONFIG_FLASH_SECTOR_A_ADDR)
                    ? CONFIG_FLASH_SECTOR_B_ADDR
                    : CONFIG_FLASH_SECTOR_A_ADDR;

    uint8_t buf[CONFIG_FLASH_SECTOR_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    uint32_t offset = 0;

    // Header
    config_header_t header = { .magic = CONFIG_MAGIC, .version = 1, .count = registry.count };
    memcpy(buf + offset, &header, sizeof(header));
    offset += sizeof(header);

    // Records
    for (int i = 0; i < registry.count; i++) {
        config_desc_t *d = &registry.descs[i];
        config_record_t rec = {
            .type = d->id, .length = sizeof(config_record_t) + d->size,
            .version = d->version, .flags = 0
        };
        if (offset + rec.length > CONFIG_FLASH_SECTOR_SIZE - 4) break;
        memcpy(buf + offset, &rec, sizeof(rec));
        offset += sizeof(rec);
        memcpy(buf + offset, d->ram, d->size);
        offset += d->size;
    }

    // Footer
    config_footer_t footer = { .terminator = 0 };
    memcpy(buf + offset, &footer, sizeof(footer));
    offset += sizeof(footer);

    // CRC
    uint16_t crc = crc16_update(0xFFFF, buf, offset);
    buf[offset] = crc & 0xFF;
    buf[offset + 1] = crc >> 8;

    // Write
    __disable_irq();  // 写 Flash 期间关中断
    if (!config_hal_erase_sector(target)) { __enable_irq(); return false; }
    if (!config_hal_write(target, buf, sizeof(buf))) { __enable_irq(); return false; }
    __enable_irq();

    if (!validate_sector(target)) return false;

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
                    &g_joystick_calib, &joystick_calib_default,
                    "joystick_calib", "min[4],max[4],center[4]");

    config_register(CONFIG_ID_GAMEPAD_SETTINGS, 1, sizeof(gamepad_settings_t),
                    &g_gamepad_settings, &gamepad_settings_default,
                    "gamepad_settings", "deadzone,led_mode");

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
        registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
    }
}
```

---

## 第 6 步：`cmd_parser.h` — 命令解析器接口

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

// 初始化命令解析器
void cmd_init(void);

// 在主循环中调用，处理 CDC 收到的数据
void cmd_poll(void);

// CDC 回调（TinyUSB 回调中调用）
void cmd_on_cdc_rx(void);
```

## 第 7 步：`cmd_parser.c` — 命令解析器实现

```c
#include "cmd_parser.h"
#include "config.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
// 接收缓冲区
// ============================================================

#define CMD_BUF_SIZE 256
static char cmd_buf[CMD_BUF_SIZE];
static uint16_t cmd_len = 0;
static volatile bool cmd_ready = false;

// ============================================================
// 发送辅助
// ============================================================

static void cmd_send(const char *str) {
    tud_cdc_write_str(str);
    tud_cdc_write_str("\r\n");
    tud_cdc_write_flush();
}

static void cmd_send_raw(const char *str) {
    tud_cdc_write_str(str);
    tud_cdc_write_flush();
}

// ============================================================
// JSON 序列化（简易版，手写）
// ============================================================

static void serialize_uint16_array(char *out, uint16_t *arr, int count) {
    strcat(out, "[");
    char tmp[16];
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(out, ",");
        sprintf(tmp, "%u", arr[i]);
        strcat(out, tmp);
    }
    strcat(out, "]");
}

static void cmd_handle_get(int id) {
    const config_desc_t *d = config_get_desc(id);
    if (!d) { cmd_send("ERR not found"); return; }

    char resp[256];
    sprintf(resp, "OK %d {", id);

    if (id == CONFIG_ID_JOYSTICK_CALIB) {
        joystick_calib_t *c = (joystick_calib_t *)d->ram;
        char tmp[64];
        strcat(resp, "\"min\":"); serialize_uint16_array(resp, c->min, 4);
        strcat(resp, ",\"max\":"); serialize_uint16_array(resp, c->max, 4);
        strcat(resp, ",\"center\":"); serialize_uint16_array(resp, c->center, 4);
    } else if (id == CONFIG_ID_GAMEPAD_SETTINGS) {
        gamepad_settings_t *s = (gamepad_settings_t *)d->ram;
        char tmp[32];
        sprintf(tmp, "\"deadzone\":%u,\"led_mode\":%u", s->deadzone, s->led_mode);
        strcat(resp, tmp);
    }
    strcat(resp, "}");
    cmd_send(resp);
}

static void cmd_handle_get_all(void) {
    for (int i = 0; i < config_get_count(); i++) {
        const config_desc_t *d = config_get_desc_by_index(i);
        if (d) cmd_handle_get(d->id);
    }
}

// ============================================================
// JSON 简易解析（只支持 SET id.field=value）
// ============================================================

// 解析 uint16 数组，如 "[1500,2000,1800,2100]"
static bool parse_uint16_array(const char *str, uint16_t *out, int max_count) {
    if (str[0] != '[') return false;
    str++;
    int count = 0;
    while (*str && *str != ']' && count < max_count) {
        out[count++] = (uint16_t)atoi(str);
        while (*str && *str != ',' && *str != ']') str++;
        if (*str == ',') str++;
    }
    return (*str == ']');
}

static void cmd_handle_set(const char *args) {
    // 格式: <id>.<field>=<value>  或  <id> <json>
    int id = atoi(args);
    const config_desc_t *d = config_get_desc(id);
    if (!d) { cmd_send("ERR not found"); return; }

    // 找到 '.' 分隔符
    const char *dot = strchr(args, '.');
    if (!dot) { cmd_send("ERR format: SET <id>.<field>=<value>"); return; }
    dot++;

    // 找到 '=' 分隔符
    const char *eq = strchr(dot, '=');
    if (!eq) { cmd_send("ERR format: SET <id>.<field>=<value>"); return; };

    // 提取字段名
    char field[32];
    int flen = eq - dot;
    if (flen >= sizeof(field)) flen = sizeof(field) - 1;
    memcpy(field, dot, flen);
    field[flen] = '\0';

    const char *value = eq + 1;

    if (id == CONFIG_ID_JOYSTICK_CALIB) {
        joystick_calib_t *c = (joystick_calib_t *)config_get_mutable(id);
        if (!c) { cmd_send("ERR get mutable failed"); return; }
        if (strcmp(field, "min") == 0) {
            if (!parse_uint16_array(value, c->min, 4)) { cmd_send("ERR parse array"); return; }
        } else if (strcmp(field, "max") == 0) {
            if (!parse_uint16_array(value, c->max, 4)) { cmd_send("ERR parse array"); return; }
        } else if (strcmp(field, "center") == 0) {
            if (!parse_uint16_array(value, c->center, 4)) { cmd_send("ERR parse array"); return; }
        } else {
            cmd_send("ERR unknown field"); return;
        }
    } else if (id == CONFIG_ID_GAMEPAD_SETTINGS) {
        gamepad_settings_t *s = (gamepad_settings_t *)config_get_mutable(id);
        if (!s) { cmd_send("ERR get mutable failed"); return; }
        if (strcmp(field, "deadzone") == 0) {
            s->deadzone = (uint16_t)atoi(value);
        } else if (strcmp(field, "led_mode") == 0) {
            s->led_mode = (uint8_t)atoi(value);
        } else {
            cmd_send("ERR unknown field"); return;
        }
    } else {
        cmd_send("ERR not supported"); return;
    }

    config_mark_dirty(id);
    cmd_send("OK");
}

// ============================================================
// 命令分发
// ============================================================

static void cmd_dispatch(const char *line) {
    // 跳过前导空格
    while (*line == ' ') line++;

    if (strncmp(line, "GET ALL", 7) == 0) {
        cmd_handle_get_all();
    } else if (strncmp(line, "GET ", 4) == 0) {
        cmd_handle_get(atoi(line + 4));
    } else if (strncmp(line, "SET ", 4) == 0) {
        cmd_handle_set(line + 4);
    } else if (strcmp(line, "SAVE") == 0) {
        if (config_save()) cmd_send("OK saved");
        else cmd_send("ERR save failed");
    } else if (strncmp(line, "RESET ALL", 9) == 0) {
        config_reset_all();
        cmd_send("OK reset all");
    } else if (strncmp(line, "RESET ", 6) == 0) {
        if (config_reset((uint16_t)atoi(line + 6)))
            cmd_send("OK reset");
        else
            cmd_send("ERR not found");
    } else if (strcmp(line, "INFO") == 0) {
        char resp[128];
        sprintf(resp, "OK {\"chip\":\"CH32V307\",\"config_count\":%d}", config_get_count());
        cmd_send(resp);
    } else if (strcmp(line, "HELP") == 0) {
        cmd_send("OK Commands: GET <id>, GET ALL, SET <id>.<field>=<value>, "
                 "SAVE, RESET <id>, RESET ALL, INFO, HELP");
    } else {
        cmd_send("ERR unknown command");
    }
}

// ============================================================
// CDC 回调和轮询
// ============================================================

void cmd_on_cdc_rx(void) {
    // 从 CDC 读取数据到缓冲区
    while (tud_cdc_available() && cmd_len < CMD_BUF_SIZE - 1) {
        char ch = (char)tud_cdc_read_char();
        if (ch == '\n' || ch == '\r') {
            if (cmd_len > 0) {
                cmd_buf[cmd_len] = '\0';
                cmd_ready = true;
            }
        } else {
            cmd_buf[cmd_len++] = ch;
        }
    }
}

void cmd_poll(void) {
    if (cmd_ready) {
        cmd_dispatch(cmd_buf);
        cmd_len = 0;
        cmd_ready = false;
    }
}

void cmd_init(void) {
    cmd_len = 0;
    cmd_ready = false;
}
```

---

## 第 8 步：修改 `tusb_config.h`

```c
// 修改这两行
#define CFG_TUD_HID               1
#define CFG_TUD_CDC               1      // 原来是 0
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// 添加 CDC 缓冲区
#define CFG_TUD_HID_EP_BUFSIZE    64
#define CFG_TUD_CDC_EP_BUFSIZE    64
```

## 第 9 步：修改 `ds4_descriptors.h`

需要修改的部分：

### 9.1 端点地址定义

```c
// 原来
#define GAMEPAD_ENDPOINT        1
#define GAMEPAD_ENDPOINT_OUT    0x03

// 改为（避免和 CDC 端点冲突）
#define GAMEPAD_ENDPOINT        1
#define GAMEPAD_ENDPOINT_OUT    0x02
```

### 9.2 接口数量

```c
// 原来
#define GAMEPAD_INTERFACE       0
// 接口总数：1

// 改为
#define GAMEPAD_INTERFACE       0
#define CDC_COMM_INTERFACE      1
#define CDC_DATA_INTERFACE      2
// 接口总数：3
```

### 9.3 CDC 端点定义

```c
#define CDC_NOTIFICATION_EP     0x83
#define CDC_DATA_OUT_EP         0x04
#define CDC_DATA_IN_EP          0x84
```

### 9.4 配置描述符大小

```c
// 原来
#define PS4_CONFIG1_DESC_SIZE  (9 + 9 + 9 + 7 + 7)

// 改为（加上 CDC 描述符 66 字节）
#define PS4_CONFIG1_DESC_SIZE  (9 + 9 + 9 + 7 + 7 + TUD_CDC_DESC_LEN)
```

注意：需要 `#include "tusb.h"` 才能使用 `TUD_CDC_DESCRIPTOR` 宏。

### 9.5 配置描述符内容

在现有 HID 描述符后面追加 CDC 描述符：

```c
static const uint8_t ps4_configuration_descriptor[] = {
    // Configuration descriptor (9 bytes)
    9, TUSB_DESC_CONFIGURATION,
    U16_TO_U8S_LE(PS4_CONFIG1_DESC_SIZE),
    3,                          // bNumInterfaces: HID=0, CDC_Comm=1, CDC_Data=2
    1,                          // bConfigurationValue
    0,                          // iConfiguration
    0x80,                       // bmAttributes (bus-powered)
    50,                         // bMaxPower (100mA)

    // === HID Interface (unchanged, but EP OUT changed to 0x02) ===
    // Interface descriptor
    9, TUSB_DESC_INTERFACE, GAMEPAD_INTERFACE, 0, 2,
    TUSB_CLASS_HID, 0, 0, 0,
    // HID descriptor
    9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), 0, 1, HID_DESC_TYPE_REPORT,
    U16_TO_U8S_LE(sizeof(ps4_hid_report_descriptor)),
    // Endpoint IN
    7, TUSB_DESC_ENDPOINT, GAMEPAD_ENDPOINT | TUSB_DIR_IN,
    TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(64), 1,
    // Endpoint OUT (地址改为 0x02)
    7, TUSB_DESC_ENDPOINT, GAMEPAD_ENDPOINT_OUT,
    TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(64), 1,

    // === CDC Interface ===
    TUD_CDC_DESCRIPTOR(CDC_COMM_INTERFACE, 4,
                       CDC_NOTIFICATION_EP, 8,
                       CDC_DATA_OUT_EP, CDC_DATA_IN_EP, 64)
};
```

### 9.6 字符串描述符

需要添加 CDC 的字符串描述符（索引 4）：

```c
static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: Language
    "P1nk",                      // 1: Manufacturer
    "WCH_DS4",                   // 2: Product
    "123456",                    // 3: Serial
    "CDC Config",                // 4: CDC Interface（新增）
};
```

同时更新 `tud_descriptor_string_cb` 中的字符串数量。

---

## 第 10 步：修改 `ds4_driver.c` — 添加 CDC 回调

```c
// 添加 CDC 回调函数
void tud_cdc_rx_cb(uint8_t itf) {
    (void)itf;
    cmd_on_cdc_rx();
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)dtr;
    (void)rts;
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    (void)itf;
    (void)coding;
}
```

## 第 11 步：修改 `main.c`

```c
#include "config/config.h"
#include "config/cmd_parser.h"

int main(void)
{
    board_init();
    config_init();         // ① 加载配置
    cmd_init();            // ② 初始化命令解析器
    gamepad_init();
    tud_init(BOARD_TUD_RHPORT);

    printf("SystemClk:%d\r\n", SystemCoreClock);

    while(1)
    {
        tud_task();
        cmd_poll();        // ③ 处理 CDC 命令
        gamepad_process();
        ds4_process(gamepad_get_state());
    }
}
```

---

## 第 12 步：网页端（cmd_web.h）

将 HTML/JS 文件内嵌为 C 字符串数组，通过 CDC 发送。或者用户手动在浏览器打开 HTML 文件。

网页核心功能：
1. WebSerial API 连接虚拟串口
2. 发送 `GET ALL` 读取所有配置
3. 渲染为表单
4. 修改后发送 `SET` 命令
5. `SAVE` 保存

网页代码单独维护为 HTML 文件，不内嵌到固件中（节省 Flash 空间）。

---

## 添加新配置组

3 步：

1. **config.h** 中添加结构体和枚举：
```c
enum { ... CONFIG_ID_LED_CONFIG = 3 };
typedef struct { uint8_t brightness, r, g, b; } led_config_t;
extern led_config_t g_led_config;
```

2. **config.c** 中添加实例、默认值、注册：
```c
led_config_t g_led_config;
static const led_config_t led_config_default = { .brightness=128, .r=0, .g=0, .b=255 };
// 在 config_init() 中：
config_register(CONFIG_ID_LED_CONFIG, 1, sizeof(led_config_t),
                &g_led_config, &led_config_default, "led_config", "brightness,r,g,b");
```

3. **cmd_parser.c** 中添加 GET/SET 处理分支。

---

## 调试步骤

1. 先单独测试 Flash 擦写（`config_hal`）
2. 测试配置存储（写入 → 断电 → 读取）
3. 测试 CDC 串口（用串口工具连接，发送 HELP）
4. 测试命令解析（GET/SET/SAVE）
5. 测试网页 WebSerial
