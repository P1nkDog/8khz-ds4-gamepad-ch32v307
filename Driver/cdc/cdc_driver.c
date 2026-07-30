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
    while (tud_cdc_available()) {
        uint8_t byte;
        tud_cdc_read(&byte, 1);
        msp_process_byte(byte);
    }
}