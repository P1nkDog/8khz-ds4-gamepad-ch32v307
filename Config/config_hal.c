/********************************** (C) COPYRIGHT  *******************************
* File Name          : config_hal.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/19
* Description        : flash擦写实现
*********************************************************************************/

#include "config_hal.h"
#include "ch32v30x_flash.h"
#include <string.h>




//擦除扇区
bool config_hal_erase_sector(uint32_t sector_addr){
    FLASH_Status status = FLASH_ROM_ERASE(sector_addr, CONFIG_FLASH_SECTOR_SIZE);
    return (status == FLASH_COMPLETE);
}

//向flash写入数据
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len){
    uint32_t aligned_len = (len + 3) & ~3;

    static uint32_t write_buf[CONFIG_FLASH_SECTOR_SIZE / 4];
    memset(write_buf, 0xFF, aligned_len);
    memcpy(write_buf, data, len);

    FLASH_Status status = FLASH_ROM_WRITE(addr, write_buf, aligned_len);
    return (status == FLASH_COMPLETE);
}

//读取数据
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len){
    const uint8_t *src = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++){
        buf[i] = src[i];
    }
}