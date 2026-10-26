/********************************** (C) COPYRIGHT  *******************************
* File Name          : ds4_driver.h
* Author             : P1nkDog
* Version            : V1.1.0
* Date               : 2026/07/02
* Description        : ds4驱动接口头文件
*********************************************************************************/

#ifndef DS4_DRIVER_H
#define DS4_DRIVER_H

#include "ds4_descriptors.h"
#include "tusb.h"
#include "gamepad.h"

// PS4 Feature Report IDs（参考 GP2040-CE PS4AuthReport）
typedef enum
{
    PS4_GET_CALIBRATION      = 0x02,    // PS4 Controller Calibration
    PS4_DEFINITION           = 0x03,    // PS4 Controller Definition
    PS4_SET_FEATURE_STATE    = 0x05,    // PS4 Controller Features
    PS4_GET_MAC_ADDRESS      = 0x12,    // PS4 Controller MAC
    PS4_SET_HOST_MAC         = 0x13,    // Set Host MAC
    PS4_SET_USB_BT_CONTROL   = 0x14,    // Set USB/BT Control Mode
    PS4_GET_VERSION_DATE     = 0xA3,    // PS4 Controller Version & Date
    PS4_SET_AUTH_PAYLOAD     = 0xF0,    // Set Auth Payload
    PS4_GET_SIGNATURE_NONCE  = 0xF1,    // Get Signature Nonce
    PS4_GET_SIGNING_STATE    = 0xF2,    // Get Signing State
    PS4_RESET_AUTH           = 0xF3     // Unknown (PS4 Report 0xF3)
} PS4AuthReport;

// PS4 Auth buffer must be used by callbacks, core0, and core1
// Send back in 56 byte chunks:
//    256 byte - nonce signature
//    16 byte  - ps4 serial
//    256 byte - RSA N
//    256 byte - RSA E
//    256 byte - ps4 signature
//    24 byte  - zero padding


// 初始化
void ds4_driver_init(void);

// 端点是否可发送（内含 1ms 超时兜底，防止完成回调丢失导致卡死）
bool ds4_ready_to_send(void);

// 主循环处理（参考 PS4Driver::process）
void ds4_process(const GamepadState *state);

// 累计丢帧数（供调试/MSP上报）
uint32_t ds4_get_drop_count(void);

// TinyUSB 描述符回调
// uint8_t const *tud_descriptor_device_cb(void);
// uint8_t const *tud_descriptor_configuration_cb(uint8_t index);
// uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid); 

// TinyUSB HID 回调
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance);
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t* buffer, uint16_t reqlen);
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const* buffer, uint16_t bufsize);

// TinyUSB HID 发送完成回调（覆盖弱符号，用于事件驱动发送）
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len);

// 手柄数据发送
bool ds4_send_report(const PS4Report *report);





#endif // DS4_DRIVER_H