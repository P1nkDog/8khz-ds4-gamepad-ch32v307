/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : Main program body.
********************************************************************************/



#include "debug.h"
#include "board_api.h"
#include "ds4_driver.h"
#include "gamepad.h"


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
    gamepad_init();
    tud_init(BOARD_TUD_RHPORT);
	printf("SystemClk:%d\r\n",SystemCoreClock);
	printf( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );
	printf("This is printf example\r\n");



	while(1)
    {
        tud_task();
        gamepad_process();
        ds4_process(gamepad_get_state());
        board_delay(1);
	}
}

