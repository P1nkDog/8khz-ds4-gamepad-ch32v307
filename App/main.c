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

    tud_init(BOARD_TUD_RHPORT);

    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    printf("Mode: %s\r\n", current_mode == USB_MODE_HID ? "HID" : "CDC");

    gamepad_init();
    
    uint32_t send_interval_us = config_get_poll_interval_us();
    uint32_t last_send_us = 0;

    while (1)
    {
        tud_task();

        if (current_mode == USB_MODE_HID) {
            uint32_t now_us = micros();
            if ((now_us - last_send_us) >= send_interval_us) {
                gamepad_process();
                ds4_process(gamepad_get_state());
                last_send_us = now_us;
            }
        } else {
            cdc_process();
            joystick_process();
        }
    }
}