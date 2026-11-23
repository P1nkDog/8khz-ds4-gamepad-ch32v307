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


//硬件页大小：FLASH_ROM_WRITE() 内部按 64 word(256B) 一页做页编程
#define CONFIG_FLASH_PAGE_SIZE 256U


//擦除扇区
//  4KB 扇区擦除是一次不可分割的硬件操作，只能整段保护；
//  这段窗口通常是毫秒级，也是本路径上仅剩的最长一段关中断。
bool config_hal_erase_sector(uint32_t sector_addr){
    __disable_irq();
    FLASH_Status status = FLASH_ROM_ERASE(sector_addr, CONFIG_FLASH_SECTOR_SIZE);
    __enable_irq();

    return (status == FLASH_COMPLETE);
}

//向flash写入数据
//  直接把调用方的缓冲区交给 FLASH_ROM_WRITE()，不做整扇区的中间拷贝；
//  同时把写入拆成 256B 硬件页，逐页关中断，页与页之间放开中断——
//  旧实现是"整扇区 4096B 一次调用 + 外层长时间关中断"，一旦插页/擦除
//  同时被屏蔽，USBHS 与 TIM2 会被饿死 20~30ms。
bool config_hal_write(uint32_t addr, const uint8_t *data, uint32_t len){
    if (data == NULL || len == 0U) return false;

    //FLASH_ROM_WRITE 要求地址/源指针/长度 4 字节对齐；非对齐指针强转 uint32_t*
    //会触发 RISC-V 非对齐访问异常，长度非 4 倍数还会读穿缓冲区尾部。
    if (((addr & 3U) != 0U) || (((uintptr_t)data & 3U) != 0U) || ((len & 3U) != 0U)) {
        return false;
    }

    //分页对齐同样必须成立：库函数按 (Length >> 8) 算页数，
    //末页不足 256B 会直接返回 FLASH_ALIGN_ERROR，这里提前挡掉。
    if (((addr & (CONFIG_FLASH_PAGE_SIZE - 1U)) != 0U) ||
        ((len  & (CONFIG_FLASH_PAGE_SIZE - 1U)) != 0U)) {
        return false;
    }

    const uint8_t *src = data;
    for (uint32_t done = 0; done < len; done += CONFIG_FLASH_PAGE_SIZE) {
        //单页原子窗口：一页约 1~2ms，远小于 TIM2 的 65.536ms 溢出周期，
        //不会丢 tick；页间放开中断让 USB 与系统节拍得到响应。
        __disable_irq();
        FLASH_Status status = FLASH_ROM_WRITE(addr + done,
                                             (uint32_t *)(const void *)(src + done),
                                             CONFIG_FLASH_PAGE_SIZE);
        __enable_irq();

        if (status != FLASH_COMPLETE) return false;
    }

    return true;
}

//读取数据
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len){
    const uint8_t *src = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++){
        buf[i] = src[i];
    }
}