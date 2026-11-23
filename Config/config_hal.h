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
// 储存区地址
//   A = 256KB 区间的倒数第 2 个 4KB 扇区（0x0803E000 ~ 0x0803EFFF）
//   B = 256KB 区间的最后 1 个 4KB 扇区  （0x0803F000 ~ 0x0803FFFF）
//
// 选址依据：
//   1. 两扇区整体落在 0x08000000 ~ 0x08040000（256KB）之内，兼容
//      CH32V307 的全部 FLASH/RAM 分档（256K+64K / 288K+32K / 224K+96K
//      / 192K+128K 等）；而原地址 0x08047000 恰好压在 288K 分档的阵列
//      顶端（结束地址 0x08048000），一旦芯片处于 256K 分档就会落到有效
//      阵列之外，且库里的地址检查上界是 0x08078000（见 ch32v30x_flash.c
//      的 ValidAddrEnd），拦不住这种越界。
//   2. 当前固件代码段约 26KB（本次改动后 .text + .data 结束于 0x000067D0），
//      距 0x0803E000 尚有 227376 字节（约 222KB）的安全余量，不会与代码段
//      冲突；config.c 中的 _Static_assert 会把这个约束固化为编译期检查。
//   3. 末尾不占用 0x08040000 之后的地址，为将来扩大 Flash 分档留出空闲。
//--------------------------------------------------------------------+
#define CONFIG_FLASH_SECTOR_A_ADDR  0x0803E000
#define CONFIG_FLASH_SECTOR_B_ADDR  0x0803F000

//擦除扇区
bool config_hal_erase_sector(uint32_t sector_addr);

//向flash写入数据
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len);

//读取数据
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len);


#endif //CONFIG_HAL_H