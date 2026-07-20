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

//储存区地址
#define CONFIG_FLASH_SECTOR_A_ADDR  0x08046000
#define CONFIG_FLASH_SECTOR_B_ADDR  0x08047000

//擦除扇区
bool config_hal_erase_sector(uint32_t sector_addr);

//向flash写入数据
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len);

//读取数据
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len);


#endif //CONFIG_HAL_H