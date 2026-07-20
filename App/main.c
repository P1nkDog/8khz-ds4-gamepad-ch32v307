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
#include "config.h"


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
        printf("\r\n=== Config Debug ===\r\n");
    printf("Groups: %d\r\n", config_get_count());

    for (int i = 0; i < config_get_count(); i++) {
        const ConfigDesc *d = config_get_desc_by_index(i);
        printf("  [%d] id=%d ver=%d size=%d name=%s\r\n",
            i, d->id, d->version, d->size, d->name);
    }

    const JoystickCalibConfig *calib = (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
    printf("Joystick Calib:\r\n");
    for (int i = 0; i < 4; i++) {
        printf("  [%d] min=%d center=%d max=%d\r\n",
            i, calib->calib[i].min, calib->calib[i].center, calib->calib[i].max);
    }

    const GamepadSettings *s = (const GamepadSettings *)config_get(CONFIG_ID_GAMEPAD_SETTINGS);
    printf("Gamepad Settings:\r\n");
    printf("  deadzone=%d\r\n", s->deadzone);

    gamepad_init();
    tud_init(BOARD_TUD_RHPORT);
	printf("SystemClk:%d\r\n",SystemCoreClock);
	printf( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );



	while(1)
    {
        tud_task();
        gamepad_process();
        ds4_process(gamepad_get_state());
	}
}

