/********************************** (C) COPYRIGHT  *******************************
* File Name          : input.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/09
* Description        : 手柄按键ADC读取
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

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE );
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE );
	RCC_ADCCLKConfig(RCC_PCLK2_Div8);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
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
    ADC_RegularChannelConfig(ADC1,channel,1,ADC_SampleTime_239Cycles5);
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

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE );

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 |
                                  GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOE, &GPIO_InitStructure);   

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 |
                                  GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                                  GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
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
    data -> lx = adc_read_calibrated(ADC_Channel_11);
    data -> ly = adc_read_calibrated(ADC_Channel_10);
    data -> rx = adc_read_calibrated(ADC_Channel_13);
    data -> ry = adc_read_calibrated(ADC_Channel_12);
}

//读取标准按键
void input_read_buttons(ButtonData *data){
    data->dpad = INPUT_HAT_NOTHING;
    data->buttons = 0;
    data->l2 = 0;
    data->r2 = 0;

    // 十字键 (PB3-PB6)
    if(gpio_read_pin(GPIOB, GPIO_Pin_3) == 0) data->dpad = INPUT_HAT_UP;
    if(gpio_read_pin(GPIOB, GPIO_Pin_4) == 0) data->dpad = INPUT_HAT_DOWN;
    if(gpio_read_pin(GPIOB, GPIO_Pin_5) == 0) data->dpad = INPUT_HAT_LEFT;
    if(gpio_read_pin(GPIOB, GPIO_Pin_6) == 0) data->dpad = INPUT_HAT_RIGHT;

    // 扳机 L2/R2 (PB7, PD1)
    if(gpio_read_pin(GPIOB, GPIO_Pin_7) == 0) data->l2 = 255;
    if(gpio_read_pin(GPIOD, GPIO_Pin_1) == 0) data->r2 = 255;

    // 功能按键 (PE0-PE6)
    if(gpio_read_pin(GPIOE, GPIO_Pin_0) == 0) data->buttons |= INPUT_MASK_A;
    if(gpio_read_pin(GPIOE, GPIO_Pin_1) == 0) data->buttons |= INPUT_MASK_B;
    if(gpio_read_pin(GPIOE, GPIO_Pin_2) == 0) data->buttons |= INPUT_MASK_X;
    if(gpio_read_pin(GPIOE, GPIO_Pin_3) == 0) data->buttons |= INPUT_MASK_Y;
    if(gpio_read_pin(GPIOE, GPIO_Pin_4) == 0) data->buttons |= INPUT_MASK_L1;
    if(gpio_read_pin(GPIOE, GPIO_Pin_5) == 0) data->buttons |= INPUT_MASK_R1;
    if(gpio_read_pin(GPIOE, GPIO_Pin_6) == 0) data->buttons |= INPUT_MASK_SELECT;

    // 功能按键 (PD2-PD6)
    if(gpio_read_pin(GPIOD, GPIO_Pin_2) == 0) data->buttons |= INPUT_MASK_START;
    if(gpio_read_pin(GPIOD, GPIO_Pin_3) == 0) data->buttons |= INPUT_MASK_L3;
    if(gpio_read_pin(GPIOD, GPIO_Pin_4) == 0) data->buttons |= INPUT_MASK_R3;
    if(gpio_read_pin(GPIOD, GPIO_Pin_5) == 0) data->buttons |= INPUT_MASK_PS;
    if(gpio_read_pin(GPIOD, GPIO_Pin_6) == 0) data->buttons |= INPUT_MASK_TP;
}

//读取拓展按键
void input_read_extrabuttons(ExtraButtonData *data){
    data->extra = 0;

    // 扩展按键 (PD7)
    if(gpio_read_pin(GPIOD, GPIO_Pin_7) == 0) data->extra |= INPUT_MASK_A1;
}