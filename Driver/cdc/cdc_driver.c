/********************************** (C) COPYRIGHT *******************************
* File Name          : cdc_driver.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/30
* Description        : cdc虚拟串口驱动
********************************************************************************/

#include <string.h>
#include "tusb.h"
#include "cdc_driver.h"
#include "msp_handler.h"

void cdc_process(void){
    //先做半包超时检查：主机发半个帧后掉线时，状态机会一直停在非 IDLE 状态
    //继续吞字节，导致重连后第一条命令被吃掉。msp_poll() 用 50ms 字节间超时兜底。
    msp_poll();

    while (tud_cdc_available()) {
        uint8_t byte;
        tud_cdc_read(&byte, 1);
        msp_process_byte(byte);
    }
}