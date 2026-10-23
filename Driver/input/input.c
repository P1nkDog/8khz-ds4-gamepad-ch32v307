/********************************** (C) COPYRIGHT  *******************************
* File Name          : input.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : 手柄GPIO按键和摇杆ADC读取
*********************************************************************************/

#include "input.h"
#include "ch32v30x.h"

// 摇杆校准值
static int16_t adc_calibrattion_val;

//--------------------------------------------------------------------+
// ADC初始化
//--------------------------------------------------------------------+
static void adc_function_init(void)
{
	ADC_InitTypeDef ADC_InitStructure={0};
	GPIO_InitTypeDef GPIO_InitStructure={0};

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE );
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    // PA6/PA7 = 右摇杆 Y/X（JS1.X_OUT/JS1.Y_OUT，ADC6/ADC7）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PC4/PC5 = 左摇杆 X/Y（JS2.Y_OUT/JS2.X_OUT，ADC14/ADC15）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

	ADC_DeInit(ADC1);
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfChannel = 1;
	ADC_Init(ADC1, &ADC_InitStructure);

	// ADC_DMACmd(ADC1, ENABLE);
	ADC_Cmd(ADC1, ENABLE);

	ADC_BufferCmd(ADC1, DISABLE);   //disable buffer
	ADC_ResetCalibration(ADC1);
	while(ADC_GetResetCalibrationStatus(ADC1));
	ADC_StartCalibration(ADC1);
	while(ADC_GetCalibrationStatus(ADC1));
	adc_calibrattion_val = Get_CalibrationValue(ADC1);	
	
}


//--------------------------------------------------------------------+
// ADC读取原始值
//--------------------------------------------------------------------+
static uint16_t adc_read_raw(uint8_t channel){
    ADC_RegularChannelConfig(ADC1,channel,1,ADC_SampleTime_71Cycles5);
    ADC_SoftwareStartConvCmd(ADC1,ENABLE);
    while (!ADC_GetFlagStatus(ADC1,ADC_FLAG_EOC));
    return ADC_GetConversionValue(ADC1);
}

//--------------------------------------------------------------------+
// ADC读取校准值
//--------------------------------------------------------------------+
static uint16_t adc_read_calibrated(uint8_t channel){
    int32_t raw_value = adc_read_raw(channel);
    int32_t calibrated_value = raw_value + adc_calibrattion_val;
    if(calibrated_value < 0) calibrated_value = 0;
    if(calibrated_value > 4095) calibrated_value = 4095;
    return (uint16_t)calibrated_value;
}

//--------------------------------------------------------------------+
// GPIO初始化
//--------------------------------------------------------------------+
static void button_gpio_init(void){
    GPIO_InitTypeDef GPIO_InitStructure={0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

    // PE0,PE1,PE7~PE15：按键/摇杆按下（除 PE2~PE6 外的已用引脚）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 |
                                  GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                                  GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 |
                                  GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    // PB0,PB1,PB3,PB4,PB10,PB11,PB12,PB13,PB14
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3 | GPIO_Pin_4 |
                                  GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 |
                                  GPIO_Pin_14;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PD3~PD7：BTN1~BTN5
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // PA5：左摇杆按下（L3，JS2按键）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

//--------------------------------------------------------------------+
// GPIO读取
//--------------------------------------------------------------------+
static uint8_t gpio_read_pin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin){
    return GPIO_ReadInputDataBit(GPIOx,GPIO_Pin);
}





//--------------------------------------------------------------------+
// 公共接口
//--------------------------------------------------------------------+

//初始化输入硬件（GPIO，ADC）
void input_init(void){
    adc_function_init();
    button_gpio_init();
}


//读取摇杆
void input_read_joystick(JoystickData *data){
    data -> lx = adc_read_calibrated(ADC_Channel_14);   // PC4 左摇杆X
    data -> ly = adc_read_calibrated(ADC_Channel_15);   // PC5 左摇杆Y
    data -> rx = adc_read_calibrated(ADC_Channel_7);    // PA7 右摇杆X
    data -> ry = adc_read_calibrated(ADC_Channel_6);    // PA6 右摇杆Y
}

//读取标准按键
void input_read_buttons(uint32_t *buttons){

    *buttons = 0; 

    // 方向键
    if(gpio_read_pin(GPIOE, GPIO_Pin_9)  == 0) *buttons |= INPUT_MASK_UP;
    if(gpio_read_pin(GPIOE, GPIO_Pin_8)  == 0) *buttons |= INPUT_MASK_RIGHT;
    if(gpio_read_pin(GPIOE, GPIO_Pin_11) == 0) *buttons |= INPUT_MASK_DOWN;
    if(gpio_read_pin(GPIOE, GPIO_Pin_10) == 0) *buttons |= INPUT_MASK_LEFT;

    // 面键 A/B/X/Y
    if(gpio_read_pin(GPIOE, GPIO_Pin_13) == 0) *buttons |= INPUT_MASK_BTN_A;
    if(gpio_read_pin(GPIOE, GPIO_Pin_14) == 0) *buttons |= INPUT_MASK_BTN_B;
    if(gpio_read_pin(GPIOE, GPIO_Pin_15) == 0) *buttons |= INPUT_MASK_BTN_Y;
    if(gpio_read_pin(GPIOB, GPIO_Pin_11) == 0) *buttons |= INPUT_MASK_BTN_X;

    // 肩键 L1/R1/L2/R2
    if(gpio_read_pin(GPIOE, GPIO_Pin_7)  == 0) *buttons |= INPUT_MASK_L1;
    if(gpio_read_pin(GPIOB, GPIO_Pin_10) == 0) *buttons |= INPUT_MASK_R1;
    if(gpio_read_pin(GPIOB, GPIO_Pin_0)  == 0) *buttons |= INPUT_MASK_L2;
    if(gpio_read_pin(GPIOB, GPIO_Pin_1)  == 0) *buttons |= INPUT_MASK_R2;

    // 摇杆按下 L3/R3
    if(gpio_read_pin(GPIOA, GPIO_Pin_5)  == 0) *buttons |= INPUT_MASK_L3;
    if(gpio_read_pin(GPIOE, GPIO_Pin_1)  == 0) *buttons |= INPUT_MASK_R3;

    // PS / Select / Start / Touchpad
    if(gpio_read_pin(GPIOE, GPIO_Pin_12) == 0) *buttons |= INPUT_MASK_PS;
    if(gpio_read_pin(GPIOB, GPIO_Pin_12) == 0) *buttons |= INPUT_MASK_SELECT;   // SW2
    if(gpio_read_pin(GPIOB, GPIO_Pin_14) == 0) *buttons |= INPUT_MASK_START;    // SW4
    if(gpio_read_pin(GPIOB, GPIO_Pin_13) == 0) *buttons |= INPUT_MASK_TOUCHPAD; // SW3

    // 扩展按键 BTN1~BTN8
    if(gpio_read_pin(GPIOD, GPIO_Pin_3) == 0) *buttons |= INPUT_MASK_BTN1;
    if(gpio_read_pin(GPIOD, GPIO_Pin_4) == 0) *buttons |= INPUT_MASK_BTN2;
    if(gpio_read_pin(GPIOD, GPIO_Pin_5) == 0) *buttons |= INPUT_MASK_BTN3;
    if(gpio_read_pin(GPIOD, GPIO_Pin_6) == 0) *buttons |= INPUT_MASK_BTN4;
    if(gpio_read_pin(GPIOD, GPIO_Pin_7) == 0) *buttons |= INPUT_MASK_BTN5;
    if(gpio_read_pin(GPIOB, GPIO_Pin_3) == 0) *buttons |= INPUT_MASK_BTN6;
    if(gpio_read_pin(GPIOB, GPIO_Pin_4) == 0) *buttons |= INPUT_MASK_BTN7;
    if(gpio_read_pin(GPIOE, GPIO_Pin_0) == 0) *buttons |= INPUT_MASK_BTN8;
}