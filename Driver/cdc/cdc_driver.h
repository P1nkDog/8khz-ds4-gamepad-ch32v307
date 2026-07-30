/********************************** (C) COPYRIGHT *******************************
* File Name          : cdc_driver.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/30
* Description        : cdc虚拟串口驱动接口声明
********************************************************************************/

#ifndef CDC_DRIVER_H
#define CDC_DRIVER_H

#include <stdint.h>

//cdc主循环
void cdc_process(void);

#endif //CDC_DRIVER_H