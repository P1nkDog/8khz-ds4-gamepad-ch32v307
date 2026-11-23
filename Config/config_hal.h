/********************************** (C) COPYRIGHT  *******************************
* File Name          : config_hal.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/19
* Description        : flash接口声明
*********************************************************************************/

#ifndef CONFIG_HAL_H
#define CONFIG_HAL_H

#include <stdint.h>
#include <stdbool.h>

//扇区大小 4kb
#define CONFIG_FLASH_SECTOR_SIZE 0x1000

//--------------------------------------------------------------------+
// 储存区地址：Flash 末尾 40KB 划分为 5 组独立 A/B 双扇区
//
//   | 层           | A 扇区        | B 扇区        | 说明                     |
//   |--------------|---------------|---------------|--------------------------|
//   | 全局硬件层   | 0x08036000    | 0x08037000    | 与"哪一套配置"无关的参数 |
//   | 配置 1 (0)   | 0x08038000    | 0x08039000    | 独立配置层               |
//   | 配置 2 (1)   | 0x0803A000    | 0x0803B000    | 独立配置层               |
//   | 配置 3 (2)   | 0x0803C000    | 0x0803D000    | 独立配置层               |
//   | 配置 4 (3)   | 0x0803E000    | 0x0803F000    | 独立配置层               |
//
// 选址依据：
//   1. 五组整体落在 0x08000000 ~ 0x08040000（256KB）之内，兼容
//      CH32V307 的全部 FLASH/RAM 分档（256K+64K / 288K+32K / 224K+96K
//      / 192K+128K 等）；而更早用过的 0x08047000 恰好压在 288K 分档的阵列
//      顶端（结束地址 0x08048000），一旦芯片处于 256K 分档就会落到有效
//      阵列之外，且库里的地址检查上界是 0x08078000（见 ch32v30x_flash.c
//      的 ValidAddrEnd），拦不住这种越界。
//   2. 当前固件镜像实测止于 Flash 内 0x08007484（.text 结束 0x7374 +
//      .data 0x110，共 29 828 字节 = 29.13KB，与 .bin 大小逐字节一致），
//      距 0x08036000 尚有 191 356 字节（约 186.87KB）安全余量，不会与
//      代码段冲突。该约束固化为两道机器检查：Bsp/Link.ld 末尾的 ASSERT
//      （链接期，镜像一旦长到 0x08036000 直接报错）与 config.c 中的
//      _Static_assert（编译期，校验扇区地址本身的合法性与对齐）。
//   3. A/B 相邻成对：A 是主读扇区，B 是擦写暂存区（先写 B、CRC 校验通过
//      后再覆盖 A），掉电发生在任意一步都能从另一扇区恢复。
//   4. 每套独立配置在自己的 4KB 扇区内自行序列化（TLV），8 组宏固定排在
//      TLV 流尾段，前段留给基础参数与后续扩展。
//--------------------------------------------------------------------+

//全局硬件层（摇杆校准 / 回报率 / 全局系统设置）
#define CONFIG_GLOBAL_SECTOR_A_ADDR  0x08036000UL
#define CONFIG_GLOBAL_SECTOR_B_ADDR  0x08037000UL

//独立配置套数（配置 1 ~ 配置 4）
#define CONFIG_PROFILE_COUNT         4

//独立配置层起始地址与每套跨度（A/B 两个 4KB 扇区 = 8KB）
#define CONFIG_PROFILE_SECTOR_BASE_ADDR  0x08038000UL
#define CONFIG_PROFILE_SECTOR_STRIDE     0x2000UL

//第 idx 套（idx = 0 ~ CONFIG_PROFILE_COUNT-1）的 A/B 扇区基址
//  A = BASE + idx * 8KB
//  B = A + 4KB
#define CONFIG_PROFILE_SECTOR_A_ADDR(idx) \
    (CONFIG_PROFILE_SECTOR_BASE_ADDR + ((uint32_t)(idx) * CONFIG_PROFILE_SECTOR_STRIDE))
#define CONFIG_PROFILE_SECTOR_B_ADDR(idx) \
    (CONFIG_PROFILE_SECTOR_A_ADDR(idx) + CONFIG_FLASH_SECTOR_SIZE)

//擦除扇区
bool config_hal_erase_sector(uint32_t sector_addr);

//向flash写入数据
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len);

//读取数据
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len);


#endif //CONFIG_HAL_H
