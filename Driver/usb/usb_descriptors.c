/********************************** (C) COPYRIGHT  *******************************
* File Name          : usb_descriptors.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/29
* Description        : USB描述符路由层，根据模式返回HID或CDC描述符
*********************************************************************************/

#include <string.h>
#include "tusb.h"
#include "debug.h"
#include "usb_descriptors.h"
#include "ds4_descriptors.h"
#include "cdc_descriptors.h"
#include "ch32v30x.h"

//--------------------------------------------------------------------+
// 全局模式变量
//--------------------------------------------------------------------+
UsbMode current_mode = USB_MODE_HID;

//--------------------------------------------------------------------+
// 初始化：读取 PB12（Select / SW2）按键，决定工作模式
//--------------------------------------------------------------------+
void usb_desc_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 使能 GPIOB 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // 配置 PB12 为上拉输入
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 等待上拉电阻将引脚拉高（烧录后引脚浮空，需要时间稳定）
    Delay_Ms(10);

    // 读取按键状态：低电平 = 按下 = CDC 模式
    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12) == Bit_RESET) {
        current_mode = USB_MODE_CDC;
    } else {
        current_mode = USB_MODE_HID;
    }
}

//--------------------------------------------------------------------+
// TinyUSB Device Descriptor 回调（路由）
//--------------------------------------------------------------------+
uint8_t const *tud_descriptor_device_cb(void) {
    if (current_mode == USB_MODE_HID) {
        return ps4_device_descriptor;
    } else {
        return (uint8_t const *)&cdc_device_descriptor;
    }
}

//--------------------------------------------------------------------+
// TinyUSB Configuration Descriptor 回调（路由）
//--------------------------------------------------------------------+
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    if (current_mode == USB_MODE_HID) {
        return ps4_configuration_descriptor;
    } else {
        return cdc_configuration_descriptor;
    }
}

//--------------------------------------------------------------------+
// TinyUSB Device Qualifier 回调
//--------------------------------------------------------------------+
uint8_t const *tud_descriptor_device_qualifier_cb(void) {
    return NULL;  // CH32V307 只支持高速，不支持退回全速，STALL 此请求
}

//--------------------------------------------------------------------+
// TinyUSB String Descriptor 回调（路由）
//--------------------------------------------------------------------+
static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    // HID 模式的字符串描述符表
    static char const *hid_string_desc_arr[] = {
        (const char *)ps4_string_language,
        (const char *)ps4_string_manufacturer,
        (const char *)ps4_string_product,
        NULL,  // serial
    };
    size_t hid_string_count = sizeof(hid_string_desc_arr) / sizeof(hid_string_desc_arr[0]);

    // CDC 模式的字符串描述符表
    static char const *cdc_string_desc_arr[] = {
        (const char *)cdc_string_language,
        (const char *)cdc_string_manufacturer,
        (const char *)cdc_string_product,
        NULL,  // serial
        (const char *)cdc_string_cdc_interface,
    };
    size_t cdc_string_count = sizeof(cdc_string_desc_arr) / sizeof(cdc_string_desc_arr[0]);

    // 根据模式选择字符串表
    char const **string_arr;
    size_t string_count;
    if (current_mode == USB_MODE_HID) {
        string_arr = hid_string_desc_arr;
        string_count = hid_string_count;
    } else {
        string_arr = cdc_string_desc_arr;
        string_count = cdc_string_count;
    }

    switch (index) {
        case 0: // LANGID
            memcpy(&_desc_str[1], string_arr[0], 2);
            chr_count = 1;
            break;

        case 3: { // Serial Number（硬编码，因为 board_init.c 没实现 board_usb_get_serial）
            static const char serial[] = "00000001";
            chr_count = strlen(serial);
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = serial[i];
            }
            break;
        }

        default:
            if (index >= string_count) {
                return NULL;
            }
            const char *str = string_arr[index];
            if (str == NULL) return NULL;

            chr_count = strlen(str);
            size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_count) chr_count = max_count;

            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}