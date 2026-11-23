/********************************** (C) COPYRIGHT  *******************************
* File Name          : config_hal.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/19
* Description        : flash擦写实现
*********************************************************************************/

#include "config_hal.h"
#include "ch32v30x_flash.h"
#include <stddef.h>




//擦除扇区
bool config_hal_erase_sector(uint32_t sector_addr){
    FLASH_Status status = FLASH_ROM_ERASE(sector_addr, CONFIG_FLASH_SECTOR_SIZE);
    return (status == FLASH_COMPLETE);
}

//向flash写入数据
//  直接把调用方的缓冲区交给 FLASH_ROM_WRITE()，不再做整扇区的中间拷贝：
//  FLASH_ROM_WRITE 内部按 64 word(256B)/页 编程，要求目标地址、源指针、长度
//  都是 4 字节对齐；非对齐指针强转 uint32_t* 会触发 RISC-V 非对齐访问异常，
//  长度非 4 倍数还会读出缓冲区尾部之外的数据，因此这里先做严格校验。
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len){
    if (data == NULL || len == 0U) return false;

    if (((addr & 3U) != 0U) || (((uintptr_t)data & 3U) != 0U) || ((len & 3U) != 0U)) {
        return false;
    }

    FLASH_Status status = FLASH_ROM_WRITE(addr, (uint32_t *)(const void *)data, len);
    return (status == FLASH_COMPLETE);
}

//读取数据
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len){
    const uint8_t *src = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++){
        buf[i] = src[i];
    }
}