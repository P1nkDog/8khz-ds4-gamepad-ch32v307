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
#include "input.h"


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
	JoystickData joy;

	printf("SystemClk:%d\r\n",SystemCoreClock);
	printf( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );
	printf("This is printf example\r\n");
	input_init();
	   // 打印启动信息
    printf("Input Test Start\r\n");



	while(1)
    {
		// 读取摇杆
        input_read_joystick(&joy);

        // 打印摇杆数据
        printf("LX:%04d LY:%04d RX:%04d RY:%04d\r\n",
               joy.lx, joy.ly, joy.rx, joy.ry);

        // 延时
        board_delay(100);
	}
}

