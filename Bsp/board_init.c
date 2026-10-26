/********************************** (C) COPYRIGHT  *******************************
* File Name          : board_init.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/01
* Description        : 系统初始化
*********************************************************************************/

#include "stdio.h"

#include "debug.h"
#include "ch32v30x.h"
#include "ch32v30x_it.h"

#include "board_api.h"
#include "board.h"

//--------------------------------------------------------------------+
// 将USB中断事件转发到TinyUSB IRQ处理器
//--------------------------------------------------------------------+

// TODO maybe having FS as port0, HS as port1

__attribute__((interrupt)) void USBHS_IRQHandler(void) {
  #if CFG_TUD_WCH_USBIP_USBHS
  tusb_int_handler(BOARD_TUD_RHPORT, true);
  #endif
}

__attribute__((interrupt)) void OTG_FS_IRQHandler(void) {
  #if CFG_TUD_WCH_USBIP_USBFS
  tusb_int_handler(BOARD_TUD_RHPORT, true);
  #endif
}

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM 宏类型定义常数枚举
//--------------------------------------------------------------------+

static uint32_t SysTick_Config(uint32_t ticks) {            //SysTick 配置
  NVIC_EnableIRQ(SysTicK_IRQn);
  SysTick->CTLR = 0;
  SysTick->SR = 0;
  SysTick->CNT = 0;
  SysTick->CMP = ticks - 1;
  SysTick->CTLR = 0xF;
  return 0;
}

void board_init(void) {                                    //板级总初始化函数

  /* Disable interrupts during init */
  __disable_irq();                                         //关闭全局中断

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);         //NVIC优先级分组
	SystemCoreClockUpdate();                                //更新系统时钟
  Delay_Init();                                           //延时函数初始化



#if CFG_TUSB_OS == OPT_OS_NONE
  SysTick_Config(SystemCoreClock / 1000);
#endif

  USART_Printf_Init(CFG_BOARD_UART_BAUDRATE);             //初始化 UART 串口

#ifdef CH32V30x_D8C
  // v305/v307: Highspeed USB
  RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);      //USBHS 时钟配置
  RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
  RCC_USBHSConfig(RCC_USBPLL_Div2);
  RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
  RCC_USBHSPHYPLLALIVEcmd(ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
#endif

  // Fullspeed USB
  uint8_t otg_div;
  switch (SystemCoreClock) {
    case 48000000:  otg_div = RCC_OTGFSCLKSource_PLLCLK_Div1; break;
    case 96000000:  otg_div = RCC_OTGFSCLKSource_PLLCLK_Div2; break;
    case 144000000: otg_div = RCC_OTGFSCLKSource_PLLCLK_Div3; break;
    default: TU_ASSERT(0,); break;
  }
  RCC_OTGFSCLKConfig(otg_div);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_OTG_FS, ENABLE);

  GPIO_InitTypeDef GPIO_InitStructure = {0};

  // LED
  LED_CLOCK_EN();
  GPIO_InitStructure.GPIO_Pin = LED_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(LED_PORT, &GPIO_InitStructure);

  // Button
#ifdef BUTTON_PORT
  BUTTON_CLOCK_EN();
  GPIO_InitStructure.GPIO_Pin = BUTTON_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(BUTTON_PORT, &GPIO_InitStructure);
#endif
  /* Enable interrupts globally */
  __enable_irq();

  board_delay(2);
}

#if CFG_TUSB_OS == OPT_OS_NONE
volatile uint32_t system_ticks = 0;

__attribute__((interrupt)) void SysTick_Handler(void) {
  SysTick->SR = 0;
  system_ticks++;
}

uint32_t tusb_time_millis_api(void) {
  return system_ticks;
}

#endif

//--------------------------------------------------------------------+
// Board porting API
//--------------------------------------------------------------------+

void board_led_write(bool state) {
  GPIO_WriteBit(LED_PORT, LED_PIN, state);
}

uint32_t board_button_read(void) {
#ifdef BUTTON_PORT
  return BUTTON_STATE_ACTIVE == GPIO_ReadInputDataBit(BUTTON_PORT, BUTTON_PIN);
#else
  return false;
#endif
}

int board_uart_read(uint8_t* buf, int len) {
  (void) buf;
  (void) len;
  return 0;
}

int board_uart_write(void const* buf, int len) {
  // int txsize = len;
  // const char* bufc = (const char*) buf;
  // while (txsize--) {
  //   uart_write(*bufc++);
  // }
  // uart_sync();
  // return len;
  (void) buf;
  (void) len;
  return 0;
}

size_t board_get_unique_id(uint8_t id[], size_t max_len) {
  (void) id;
  (void) max_len;
  return 0;
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(char* file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line)
   */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
