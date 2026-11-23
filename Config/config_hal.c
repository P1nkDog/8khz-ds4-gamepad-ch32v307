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


//--------------------------------------------------------------------+
// 临界区原语：保存并恢复 mstatus.MIE
//   __disable_irq() / __enable_irq() 在 core_riscv.h 里是无条件写 mstatus
//   （csrc / csrs 0x800, 0x88），只要在"本来就已经关中断"的上下文（ISR 内部、
//   或外层临界区）里调用，返回时就会把中断提前打开，破坏外层临界区语义。
//   这里改成：先读出旧 MIE 并原子地清掉、事情办完再按旧值恢复，天然支持嵌套。
//     csrrci rd, 0x800, 0x8 —— 一条指令内完成"读 mstatus 到 rd + 清 MIE(bit3)"，
//     读出的是"关中断之前"的原始值，因此可据此判断调用者原本是否允许中断。
//--------------------------------------------------------------------+
#if defined(__riscv)
static inline uint32_t flash_irq_save(void)
{
    uint32_t mstatus;
    __asm volatile ("csrrci %0, 0x800, 0x8" : "=r" (mstatus) : : "memory");
    __asm volatile ("fence.i");
    return mstatus;
}

static inline void flash_irq_restore(uint32_t mstatus)
{
    //只有调用者原本是开中断状态才恢复成开中断；原本就关着就保持关着。
    if ((mstatus & 0x8U) != 0U) {
        __asm volatile ("csrsi 0x800, 0x8" : : : "memory");
    }
}
#else
//非 RISC-V 主机环境（仅供 PC 侧联合仿真测试编译，目标固件不会走这条分支）：
//  实现由测试桩提供，用于验证"嵌套关中断不被破坏"。
uint32_t config_hal_test_irq_save(void);
void     config_hal_test_irq_restore(uint32_t mstatus);
#define flash_irq_save()      config_hal_test_irq_save()
#define flash_irq_restore(m)  config_hal_test_irq_restore(m)
#endif


//擦除扇区
//  4KB 扇区擦除是一次不可分割的硬件操作，只能整段保护；
//  这段窗口通常是毫秒级，也是本路径上仅剩的最长一段关中断。
bool config_hal_erase_sector(uint32_t sector_addr){
    uint32_t mstatus = flash_irq_save();
    FLASH_Status status = FLASH_ROM_ERASE(sector_addr, CONFIG_FLASH_SECTOR_SIZE);
    flash_irq_restore(mstatus);

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
        uint32_t mstatus = flash_irq_save();
        FLASH_Status status = FLASH_ROM_WRITE(addr + done,
                                             (uint32_t *)(const void *)(src + done),
                                             CONFIG_FLASH_PAGE_SIZE);
        flash_irq_restore(mstatus);

        if (status != FLASH_COMPLETE) return false;
    }

    return true;
}

//读取数据
void config_hal_read(uint32_t addr, uint8_t *buf, uint32_t len){
    if (buf == NULL || len == 0U) return;

    const uint8_t *src = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++){
        buf[i] = src[i];
    }
}