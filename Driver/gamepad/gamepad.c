/********************************** (C) COPYRIGHT  *******************************
* File Name          : gamepad.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/10
* Description        : GPIO按键和摇杆ADC数据映射
*********************************************************************************/

#include "gamepad.h"
#include "input.h"
#include "joystick.h"
#include <string.h>

static GamepadState state; 

void gamepad_init(){
    input_init();
    joystick_init();
    memset(&state,0,sizeof(state));
    state.dpad = GAMEPAD_HAT_NOTHING;
}

uint8_t gamepad_dpad_map(uint32_t btn){

    uint8_t up    = (btn & INPUT_MASK_UP)    ? 1 : 0;
    uint8_t right = (btn & INPUT_MASK_RIGHT) ? 1 : 0;
    uint8_t down  = (btn & INPUT_MASK_DOWN)  ? 1 : 0;
    uint8_t left  = (btn & INPUT_MASK_LEFT)  ? 1 : 0;

    
    if (up && right) return GAMEPAD_HAT_UPRIGHT;
    if (down && right) return GAMEPAD_HAT_DOWNRIGHT;
    if (down && left) return GAMEPAD_HAT_DOWNLEFT;
    if (up && left) return GAMEPAD_HAT_UPLEFT ;

    if (up) return GAMEPAD_HAT_UP;
    if (right) return GAMEPAD_HAT_RIGHT;
    if (down) return GAMEPAD_HAT_DOWN ;
    if (left) return GAMEPAD_HAT_LEFT;

    return GAMEPAD_HAT_NOTHING; 
}


void gamepad_process(){

    uint32_t btn;
    const JoystickOutput *joy;

    joystick_process();
    input_read_buttons(&btn);

    joy = joystick_get_output();
    state.lx = joy->lx;
    state.ly = joy->ly;
    state.rx = joy->rx;
    state.ry = joy->ry;

    state.buttonWest         = (btn & INPUT_MASK_BTN_X) ? 1 : 0;   // X=□
    state.buttonSouth        = (btn & INPUT_MASK_BTN_A) ? 1 : 0;   // A=×
    state.buttonEast         = (btn & INPUT_MASK_BTN_B) ? 1 : 0;   // B=○
    state.buttonNorth        = (btn & INPUT_MASK_BTN_Y) ? 1 : 0;   // Y=△
    state.buttonL1           = (btn & INPUT_MASK_L1) ? 1 : 0;
    state.buttonR1           = (btn & INPUT_MASK_R1) ? 1 : 0;
    state.buttonL2           = (btn & INPUT_MASK_L2) ? 1 : 0;
    state.buttonR2           = (btn & INPUT_MASK_R2) ? 1 : 0;
    state.buttonSelect       = (btn & INPUT_MASK_SELECT) ? 1 : 0;
    state.buttonStart        = (btn & INPUT_MASK_START) ? 1 : 0;
    state.buttonL3           = (btn & INPUT_MASK_L3) ? 1 : 0;
    state.buttonR3           = (btn & INPUT_MASK_R3) ? 1 : 0;
    state.buttonHome         = (btn & INPUT_MASK_PS) ? 1 : 0;
    state.buttonTouchpad     = (btn & INPUT_MASK_TOUCHPAD) ? 1 : 0;

    state.dpad = gamepad_dpad_map(btn);
}

const GamepadState* gamepad_get_state(){
    return &state;
}


