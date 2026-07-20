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

### 为什么用两个扇区？

Flash 有一个特性：**擦除后全变 0xFF，写入只能 1→0，不能 0→1**。

如果只有一个扇区，保存时必须：擦除 → 写入。如果在擦除之后、写入之前断电了，数据就全丢了。

两个扇区交替使用：
1. 当前数据在扇区 A
2. 先把新数据写到扇区 B（扇区 A 的数据还在）
3. 验证扇区 B 写入成功
4. 擦除扇区 A（为下次保存做准备）

如果步骤 2 或 3 中间断电，扇区 A 的数据还在，下次启动时能正常加载。

## TLV 存储格式

TLV = **T**ype（类型） + **L**ength（长度） + **V**alue（数据）

```
┌─────────────────────┐
│ Header (4B)         │  magic(0xCF07) + version(1B) + count(1B)
├─────────────────────┤
│ Record 1 (6+N B)   │  type(2B) + length(2B) + version(1B) + flags(1B) + data(NB)
├─────────────────────┤
│ Record 2 ...        │
├─────────────────────┤
│ Footer (2B)         │  terminator = 0x0000（标记记录结束）
├─────────────────────┤
│ CRC16 (2B)          │  校验码（header 到 footer 的所有数据）
└─────────────────────┘
```

**为什么要用 TLV？** 因为可以存多个配置组，每个组独立版本号。加载时按 Type 查找，版本不匹配就用默认值，不影响其他组。

---

## 第 1 步：`config_hal.h` — Flash 硬件抽象层接口

```c
#ifndef CONFIG_HAL_H
#define CONFIG_HAL_H

#include <stdint.h>
#include <stdbool.h>

// CH32V307 Flash 扇区大小 4KB
#define CONFIG_FLASH_SECTOR_SIZE    0x1000

// 配置存储区地址（Flash 末尾两个扇区）
#define CONFIG_FLASH_SECTOR_A_ADDR  0x00046000
#define CONFIG_FLASH_SECTOR_B_ADDR  0x00047000

// 擦除一个扇区（4KB），addr 必须是扇区起始地址
bool config_hal_erase_sector(uint32_t sector_addr);

// 向 Flash 写入数据，addr 必须 2 字节对齐
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len);

// 从 Flash 读取数据（Flash 是内存映射的，直接读指针即可）
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len);

#endif /* CONFIG_HAL_H */
```

## 第 2 步：`config_hal.c` — Flash 底层实现

CH32V307 提供两种 Flash 编程方式：
- **标准模式**：`FLASH_ProgramHalfWord()`，每次写 2 字节，慢
- **快速模式**：`FLASH_ROM_WRITE()`，每次写 256 字节，快

这里用快速模式，参考 WCH 官方示例 `FLASH_Program`。

```c
#include "config_hal.h"
#include "ch32v30x_flash.h"
#include <string.h>

bool config_hal_erase_sector(uint32_t sector_addr) {
    // FLASH_ROM_ERASE(起始地址, 长度)
    // 快速擦除，一次可以擦除多个扇区
    FLASH_Status status = FLASH_ROM_ERASE(sector_addr, CONFIG_FLASH_SECTOR_SIZE);
    return (status == FLASH_COMPLETE);
}

bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    // FLASH_ROM_WRITE 要求：
    //   1. 数据长度必须是 4 的倍数
    //   2. 数据缓冲区必须 4 字节对齐（uint32_t 数组）
    //   3. 地址必须 4 字节对齐

    // 将长度向上取整到 4 的倍数
    uint32_t aligned_len = (len + 3) & ~3;

    // 用 uint32_t 数组作为缓冲区，自动满足 4 字节对齐
    // CONFIG_FLASH_SECTOR_SIZE / 4 = 1024 个 uint32_t
    static uint32_t write_buf[CONFIG_FLASH_SECTOR_SIZE / 4];
    memset(write_buf, 0xFF, aligned_len);  // 未使用的部分填 0xFF
    memcpy(write_buf, data, len);          // 复制实际数据

    // FLASH_ROM_WRITE(地址, uint32_t指针, 长度)
    FLASH_Status status = FLASH_ROM_WRITE(addr, write_buf, aligned_len);
    return (status == FLASH_COMPLETE);
}

void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len) {
    // Flash 是内存映射的，直接用指针读
    const uint8_t *src = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = src[i];
    }
}
```

### 重要注意事项

**1. 写 Flash 时 CPU 不能从 Flash 取指令**

CH32V307 写 Flash 期间，CPU 无法同时从 Flash 读取代码。如果写 Flash 时有中断触发，CPU 要去执行中断处理函数（在 Flash 里），就会卡死。

解决方案：写 Flash 前关中断。`__disable_irq()` 和 `__enable_irq()` 定义在 `Core/core_riscv.h` 中，确认你的项目包含了这个头文件。

**2. 如果系统时钟超过 120MHz**

根据 WCH 官方示例的注释，如果系统时钟 > 120MHz，写 Flash 前需要将 HCLK 分频到 60MHz 以下。你的项目默认 144MHz，但 `system_ch32v30x.c` 中已经配置了 Flash 等待周期，一般不需要额外处理。如果遇到写入失败，检查 `FLASH_CTLR` 寄存器的 `SCKMOD` 位。

**3. `static` 缓冲区**

`write_buf` 用 `static` 声明，这样它在 RAM 中有固定地址，不会占用栈空间（4KB 栈上分配可能导致栈溢出）。

## 第 3 步：`config_crc.h` — CRC16-CCITT

CRC（循环冗余校验）用于检测数据是否损坏。这里用的是 CRC16-CCITT 算法，多项式 0x1021。

查表法：预先计算好 256 种可能的输入对应的 CRC 值，运行时直接查表，比逐位计算快得多。

```c
#ifndef CONFIG_CRC_H
#define CONFIG_CRC_H

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

// 计算 CRC16-CCITT
// crc: 初始值传 0xFFFF
// data: 数据指针
// len: 数据长度
// 返回值: 计算后的 CRC
static inline uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

#endif /* CONFIG_CRC_H */
```

---

## 第 4 步：`config.h` — 核心头文件

```c
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 配置组编号
// ============================================================
// 每个配置组有唯一的编号，存储时用这个编号标识是哪个组
// 新增配置组时在这里加一个编号即可

enum {
    CONFIG_ID_JOYSTICK_CALIB   = 1,   // 摇杆校准数据
    CONFIG_ID_GAMEPAD_SETTINGS = 2,   // 手柄设置
};

// ============================================================
// 引用 joystick.h 中的结构体
// ============================================================
#include "joystick.h"  // JoystickCalibration, JoystickOutput

// ============================================================
// 配置组数据结构
// ============================================================

// 摇杆校准数据（复用 joystick.h 中的 JoystickCalibration）
// 4 个轴，每轴有 min/center/max 三个校准值
// 总大小：4 × 6 字节 = 24 字节
typedef struct {
    JoystickCalibration calib[4];  // [0]=LX [1]=LY [2]=RX [3]=RY
} JoystickCalibConfig;

// 手柄设置（4 字节）
typedef struct {
    uint16_t deadzone;    // 摇杆死区（0~2047），居中时小于这个值视为 0
    uint8_t  led_mode;    // LED 模式（0=关, 1=常亮, 2=呼吸...）
    uint8_t  reserved;    // 保留，用于 4 字节对齐
} GamepadSettings;

// ============================================================
// 配置组描述符（内部使用，描述一个配置组的元信息）
// ============================================================

typedef struct {
    uint16_t    id;            // 配置组编号（CONFIG_ID_xxx）
    uint8_t     version;       // 版本号（修改结构体时递增，旧数据会被丢弃）
    uint8_t     _pad;          // 对齐填充
    uint16_t    size;          // 数据大小（字节）
    void       *ram;           // RAM 中的数据地址
    const void *default_data;  // 默认值地址
    const char *name;          // 名称（用于调试）
} ConfigDesc;

#define CONFIG_MAX_GROUPS 16  // 最多支持 16 个配置组

// ============================================================
// TLV 数据格式（存储在 Flash 中的二进制格式）
// ============================================================

#define CONFIG_MAGIC  0xCF07  // 魔数，用于识别 Flash 中是否有有效配置

// 文件头（4 字节）
typedef struct {
    uint16_t magic;     // 必须等于 CONFIG_MAGIC
    uint8_t  version;   // 存储格式版本（目前为 1）
    uint8_t  count;     // 配置组数量
} __attribute__((packed)) ConfigHeader;

// 每个配置组的记录头（6 字节）
typedef struct {
    uint16_t type;      // 配置组编号
    uint16_t length;    // 这条记录的总长度（含头部）
    uint8_t  version;   // 配置组版本号
    uint8_t  flags;     // 标志位（保留）
    uint8_t  data[];    // 柔性数组，实际数据紧跟其后
} __attribute__((packed)) ConfigRecord;

// 文件尾（2 字节）
typedef struct {
    uint16_t terminator;  // 0x0000，标记记录结束
} __attribute__((packed)) ConfigFooter;

// 注：`__attribute__((packed))` 告诉编译器不要在结构体成员之间插入填充字节
// 这样结构体在内存中的布局和我们定义的完全一致，可以直接写入 Flash

// ============================================================
// API
// ============================================================

// 初始化：注册所有配置组 + 从 Flash 加载
// 必须在其他模块初始化之前调用
void config_init(void);

// 保存所有被修改的配置到 Flash
// 只有调用过 config_mark_dirty() 的配置组才会被写入
// 返回 true 表示成功
bool config_save(void);

// 重置某个配置组为默认值
bool config_reset(uint16_t id);

// 重置所有配置为默认值
void config_reset_all(void);

// 获取配置组的只读指针
// 用法：const JoystickCalibConfig *calib = config_get(CONFIG_ID_JOYSTICK_CALIB);
const void* config_get(uint16_t id);

// 获取配置组的可写指针
// 修改后必须调用 config_mark_dirty() 标记已修改
void* config_get_mutable(uint16_t id);

// 标记某个配置组已修改（下次 config_save() 时会写入 Flash）
void config_mark_dirty(uint16_t id);

// 获取已注册的配置组数量
uint8_t config_get_count(void);

// 获取配置组描述符（用于调试）
const ConfigDesc* config_get_desc(uint16_t id);
const ConfigDesc* config_get_desc_by_index(uint8_t index);

// ============================================================
// 配置组实例（全局变量，其他文件可直接 extern 使用）
// ============================================================

extern JoystickCalibConfig    g_joystickCalib;
extern GamepadSettings  g_gamepadSettings;

#endif /* CONFIG_H */
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
// 这些变量在 RAM 中，断电丢失
// config_init() 时会从 Flash 加载数据覆盖，或用默认值初始化

JoystickCalibConfig    g_joystickCalib;
GamepadSettings  g_gamepadSettings;

// ============================================================
// 默认值（存在 Flash 的 .rodata 段，不占 RAM）
// ============================================================

static const JoystickCalibConfig joystickCalibDefault = {
    .calib = {
        [0] = { .min = 0,    .center = 2048, .max = 4095 },  // LX
        [1] = { .min = 0,    .center = 2048, .max = 4095 },  // LY
        [2] = { .min = 0,    .center = 2048, .max = 4095 },  // RX
        [3] = { .min = 0,    .center = 2048, .max = 4095 },  // RY
    },
};

static const GamepadSettings gamepadSettingsDefault = {
    .deadzone = 50,
    .led_mode = 0,
};

// ============================================================
// 注册表（记录所有已注册的配置组）
// ============================================================

static struct {
    uint8_t       count;                         // 已注册数量
    ConfigDesc descs[CONFIG_MAX_GROUPS];      // 描述符数组
    bool          dirty[CONFIG_MAX_GROUPS];      // 脏标记（true = 需要保存）
    uint32_t      active_sector;                 // 当前活动扇区地址
} registry;

// 注册一个配置组
// id: 配置组编号
// version: 版本号（修改结构体时递增）
// size: 数据大小
// ram: RAM 中的变量地址
// def: 默认值地址
// name: 名称（调试用）
static void config_register(uint16_t id, uint8_t version, uint16_t size,
                            void *ram, const void *def, const char *name) {
    if (registry.count >= CONFIG_MAX_GROUPS) return;

    ConfigDesc *d = &registry.descs[registry.count];
    d->id = id;
    d->version = version;
    d->size = size;
    d->ram = ram;
    d->default_data = def;
    d->name = name;
    registry.dirty[registry.count] = false;

    // 用默认值初始化 RAM
    memcpy(ram, def, size);

    registry.count++;
}

// ============================================================
// 查找
// ============================================================

static ConfigDesc* find_desc(uint16_t id) {
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
    ConfigDesc *d = find_desc(id);
    return d ? d->ram : NULL;
}

void* config_get_mutable(uint16_t id) {
    ConfigDesc *d = find_desc(id);
    return d ? d->ram : NULL;
}

void config_mark_dirty(uint16_t id) {
    int idx = find_index(id);
    if (idx >= 0) registry.dirty[idx] = true;
}

uint8_t config_get_count(void) {
    return registry.count;
}

const ConfigDesc* config_get_desc(uint16_t id) {
    return find_desc(id);
}

const ConfigDesc* config_get_desc_by_index(uint8_t index) {
    return (index < registry.count) ? &registry.descs[index] : NULL;
}

bool config_reset(uint16_t id) {
    ConfigDesc *d = find_desc(id);
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

// 验证一个扇区的数据是否有效
// 检查：magic 正确 + CRC 正确
static bool validate_sector(uint32_t addr) {
    uint8_t buf[CONFIG_FLASH_SECTOR_SIZE];
    config_hal_read(addr, buf, sizeof(buf));

    ConfigHeader *h = (ConfigHeader *)buf;
    if (h->magic != CONFIG_MAGIC) return false;

    // CRC 校验：对整个扇区（除最后 2 字节 CRC 本身）计算 CRC
    // 存储的 CRC 放在最后 2 字节
    uint16_t stored = buf[CONFIG_FLASH_SECTOR_SIZE - 2]
                    | (buf[CONFIG_FLASH_SECTOR_SIZE - 1] << 8);
    uint16_t calc = crc16_update(0xFFFF, buf, CONFIG_FLASH_SECTOR_SIZE - 2);
    return (stored == calc);
}

// 从指定扇区加载配置数据
static void load_sector(uint32_t addr) {
    uint8_t buf[CONFIG_FLASH_SECTOR_SIZE];
    config_hal_read(addr, buf, sizeof(buf));

    ConfigHeader *h = (ConfigHeader *)buf;
    if (h->magic != CONFIG_MAGIC) return;

    // 遍历所有 TLV 记录
    uint32_t offset = sizeof(ConfigHeader);
    while (offset + sizeof(ConfigRecord) <= CONFIG_FLASH_SECTOR_SIZE - 4) {
        ConfigRecord *rec = (ConfigRecord *)(buf + offset);

        // 遇到终止符（type=0x0000）停止
        if (rec->type == 0x0000) break;

        // 记录长度异常，停止
        if (rec->length < sizeof(ConfigRecord) ||
            offset + rec->length > CONFIG_FLASH_SECTOR_SIZE - 4) break;

        // 在注册表中查找匹配的配置组
        ConfigDesc *d = find_desc(rec->type);
        if (d && rec->version == d->version) {
            // 版本匹配，恢复数据
            uint16_t data_len = rec->length - sizeof(ConfigRecord);
            uint16_t take = data_len < d->size ? data_len : d->size;
            memcpy(d->ram, rec->data, take);
        }
        // 版本不匹配或未注册 → 保持默认值

        offset += rec->length;
    }
}

// ============================================================
// Flash 保存
// ============================================================

bool config_save(void) {
    // 1. 检查是否有脏数据
    bool has_dirty = false;
    for (int i = 0; i < registry.count; i++) {
        if (registry.dirty[i]) { has_dirty = true; break; }
    }
    if (!has_dirty) return true;  // 没有变化，不需要写入

    // 2. 选择目标扇区（当前活动扇区的另一个）
    uint32_t target = (registry.active_sector == CONFIG_FLASH_SECTOR_A_ADDR)
                    ? CONFIG_FLASH_SECTOR_B_ADDR
                    : CONFIG_FLASH_SECTOR_A_ADDR;

    // 3. 在 RAM 中构建完整的 TLV 数据
    //    这样可以一次性写入 Flash，减少写入次数
    //    代价是占用 4KB RAM（CH32V307 有 32KB RAM，够用）
    uint8_t buf[CONFIG_FLASH_SECTOR_SIZE];
    memset(buf, 0xFF, sizeof(buf));  // Flash 擦除后是 0xFF，预填充
    uint32_t offset = 0;

    // 写 Header
    ConfigHeader header = {
        .magic = CONFIG_MAGIC,
        .version = 1,
        .count = registry.count,
    };
    memcpy(buf + offset, &header, sizeof(header));
    offset += sizeof(header);

    // 写每个配置组的 Record
    for (int i = 0; i < registry.count; i++) {
        ConfigDesc *d = &registry.descs[i];
        ConfigRecord rec = {
            .type = d->id,
            .length = sizeof(ConfigRecord) + d->size,
            .version = d->version,
            .flags = 0,
        };

        // 空间检查
        if (offset + rec.length > CONFIG_FLASH_SECTOR_SIZE - 4) break;

        memcpy(buf + offset, &rec, sizeof(rec));
        offset += sizeof(rec);
        memcpy(buf + offset, d->ram, d->size);
        offset += d->size;
    }

    // 写 Footer
    ConfigFooter footer = { .terminator = 0 };
    memcpy(buf + offset, &footer, sizeof(footer));
    offset += sizeof(footer);

    // 写 CRC（放在数据之后）
    uint16_t crc = crc16_update(0xFFFF, buf, offset);
    buf[offset]     = crc & 0xFF;
    buf[offset + 1] = crc >> 8;

    // 4. 写入 Flash
    //    关中断：写 Flash 期间 CPU 不能从 Flash 取指令
    __disable_irq();
    if (!config_hal_erase_sector(target)) {
        __enable_irq();
        return false;
    }
    if (!config_hal_write(target, buf, sizeof(buf))) {
        __enable_irq();
        return false;
    }
    __enable_irq();

    // 5. 回读验证
    if (!validate_sector(target)) return false;

    // 6. 更新状态
    registry.active_sector = target;
    for (int i = 0; i < registry.count; i++) {
        registry.dirty[i] = false;
    }

    return true;
}

// ============================================================
// 初始化
// ============================================================

void config_init(void) {
    registry.count = 0;

    // 注册所有配置组
    // 参数：编号, 版本, 大小, RAM地址, 默认值地址, 名称
    config_register(CONFIG_ID_JOYSTICK_CALIB, 1, sizeof(JoystickCalibConfig),
                    &g_joystickCalib, &joystickCalibDefault, "joystick_calib");

    config_register(CONFIG_ID_GAMEPAD_SETTINGS, 1, sizeof(GamepadSettings),
                    &g_gamepadSettings, &gamepadSettingsDefault, "gamepad_settings");

    // 从 Flash 加载（如果有的话）
    bool a_ok = validate_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    bool b_ok = validate_sector(CONFIG_FLASH_SECTOR_B_ADDR);

    if (a_ok) {
        // 扇区 A 有效，从 A 加载
        registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
        load_sector(CONFIG_FLASH_SECTOR_A_ADDR);
    } else if (b_ok) {
        // 扇区 B 有效，从 B 加载
        registry.active_sector = CONFIG_FLASH_SECTOR_B_ADDR;
        load_sector(CONFIG_FLASH_SECTOR_B_ADDR);
    } else {
        // 两个扇区都无效（首次启动），使用默认值
        // 活动扇区默认指向 A
        registry.active_sector = CONFIG_FLASH_SECTOR_A_ADDR;
    }
}
```

---

## 第 6 步：修改 `main.c`

在 `main.c` 中添加配置初始化：

```c
#include "config/config.h"    // 添加头文件

int main(void)
{
    board_init();
    config_init();             // 添加这一行，必须在 gamepad_init() 之前
    gamepad_init();
    tud_init(BOARD_TUD_RHPORT);

    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    while(1)
    {
        tud_task();
        gamepad_process();
        ds4_process(gamepad_get_state());
    }
}
```

**为什么要在 `gamepad_init()` 之前？** 因为 `gamepad_init()` 可能需要用到配置数据（比如死区设置）。

---

## 使用示例

### 读取配置（只读）

```c
const JoystickCalibConfig *calib = (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
uint16_t lx_min = calib->calib[0].min;      // LX 轴最小值
uint16_t ry_center = calib->calib[3].center; // RY 轴中心值
```

### 修改配置

```c
// 1. 获取可写指针
JoystickCalibConfig *calib = (JoystickCalibConfig *)config_get_mutable(CONFIG_ID_JOYSTICK_CALIB);

// 2. 修改数据
calib->calib[0].min = 100;     // LX 轴最小值
calib->calib[0].max = 4000;    // LX 轴最大值

// 3. 标记已修改（重要！不标记的话 config_save() 不会保存这个组）
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

只需 3 步：

### 1. config.h — 添加结构体和枚举

```c
// 在 enum 中添加编号
enum {
    CONFIG_ID_JOYSTICK_CALIB   = 1,
    CONFIG_ID_GAMEPAD_SETTINGS = 2,
    CONFIG_ID_LED_CONFIG       = 3,    // 新增
};

// 添加结构体
typedef struct {
    uint8_t brightness;   // 亮度 0~255
    uint8_t r;            // 红色 0~255
    uint8_t g;            // 绿色 0~255
    uint8_t b;            // 蓝色 0~255
} LedConfig;

// 声明外部变量
extern LedConfig g_ledConfig;
```

### 2. config.c — 添加实例、默认值、注册

```c
// 添加变量
LedConfig g_ledConfig;

// 添加默认值
static const LedConfig ledConfigDefault = {
    .brightness = 128,
    .r = 0, .g = 0, .b = 255,
};

// 在 config_init() 中添加注册
config_register(CONFIG_ID_LED_CONFIG, 1, sizeof(LedConfig),
                &g_ledConfig, &ledConfigDefault, "led_config");
```

### 3. 完成

无需修改其他文件，`config_save()` 会自动包含新配置组。

---

## 调试步骤

### 第 1 步：测试 Flash 擦写

在 main.c 中单独测试，确认 Flash 读写正常：

```c
#include "config/config_hal.h"

// 测试写入和读取
uint8_t test_write[] = {0x01, 0x02, 0x03, 0x04};
uint8_t test_read[4];

config_hal_erase_sector(CONFIG_FLASH_SECTOR_A_ADDR);
config_hal_write(CONFIG_FLASH_SECTOR_A_ADDR, test_write, 4);
config_hal_read(CONFIG_FLASH_SECTOR_A_ADDR, test_read, 4);

// 检查 test_read 是否等于 {0x01, 0x02, 0x03, 0x04}
printf("Flash test: %02X %02X %02X %02X\r\n",
       test_read[0], test_read[1], test_read[2], test_read[3]);
```

### 第 2 步：测试配置存储

```c
// 修改配置
GamepadSettings *s = config_get_mutable(CONFIG_ID_GAMEPAD_SETTINGS);
s->deadzone = 100;
config_mark_dirty(CONFIG_ID_GAMEPAD_SETTINGS);
config_save();

// 重启后检查 deadzone 是否为 100
```

### 第 3 步：测试版本不匹配

修改配置组的版本号（从 1 改为 2），重新编译烧录。确认旧配置被丢弃，使用默认值。

### 第 4 步：测试双扇区交替

连续保存两次，通过 printf 打印 `registry.active_sector`，确认地址在两个扇区之间交替。
