/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : Main program body.
********************************************************************************/



#include "debug.h"
#include "board_api.h"
#include "usb_descriptors.h"
#include "ds4_driver.h"
#include "gamepad.h"
#include "cdc_driver.h"
#include "config.h"
#include "joystick.h"
#include "us_timer.h"
#include "tusb.h"
#include "usb_device.h"
#include "perf_stat.h"


/* Global typedef */

/* Global define */

/* Global Variable */


/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    board_init();
    config_init();
    us_timer_init();
    usb_desc_init();
    usb_device_init();

    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    printf("Mode: %s\r\n", current_mode == USB_MODE_HID ? "HID" : "CDC");

    gamepad_init();

    PERF_STAT_RESET();

    while (1)
    {
        tud_task();

        if (current_mode == USB_MODE_HID) {
            // 上报节奏完全由 USB 令牌驱动（bInterval 决定），软件不做节拍；
            // 未挂载时不能组帧，否则 aux 计数会被空转刷爆
            if (tud_mounted() && ds4_ready_to_send()) {
                PERF_STAT_BEGIN();
                gamepad_process();
                ds4_process(gamepad_get_state());
                PERF_STAT_END();
            }
        } else {
            cdc_process();
            joystick_process();
        }

    #if PERF_STAT_DUMP_WHEN_MOUNTED
            PERF_STAT_DUMP(tud_mounted(), ds4_get_drop_count());
    #else
            PERF_STAT_DUMP(!tud_mounted(), ds4_get_drop_count());
    #endif

    }
}