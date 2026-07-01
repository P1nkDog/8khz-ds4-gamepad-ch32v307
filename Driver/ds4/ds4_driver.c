/********************************** (C) COPYRIGHT  *******************************
* File Name          : ds4_driver.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/01
* Description        : ds4驱动文件
*********************************************************************************/

#include "board_api.h"
#include "tusb.h"
#include "ds4_descriptors.h"
#include "ds4_driver.h"

//--------------------------------------------------------------------+
// 初始化
//--------------------------------------------------------------------+
void ds4_driver_init(void) {
    // TODO: 初始化 GPIO、ADC
}

//--------------------------------------------------------------------+
// 手柄数据发送
//--------------------------------------------------------------------+
void ds4_send_report(PS4Report *report) {
    if (tud_hid_ready()) {
        tud_hid_report(0x01, report, sizeof(PS4Report));
    }
}

//--------------------------------------------------------------------+
// 主循环处理
//--------------------------------------------------------------------+
void ds4_process(void) {
    // TODO: 读取硬件，更新报告，发送
    // static uint8_t counter = 0;
    // static PS4Report report;
    // memset(&report, 0, sizeof(PS4Report));
    // report.reportID = 0x01;
    // // 读取按键、摇杆...
    // report.reportCounter = counter++ & 0x3F;
    // ds4_send_report(&report);
}





//--------------------------------------------------------------------+
// 预设的 Feature Report 数据（认证绕过，参考 GP2040-CE）
//--------------------------------------------------------------------+

// 校准数据 (Report ID 0x02, 36 bytes)
static const uint8_t ps4_output_0x02[] = {
    0xfe, 0xff, 0x0e, 0x00, 0x04, 0x00, 0xd4, 0x22,
    0x2a, 0xdd, 0xbb, 0x22, 0x5e, 0xdd, 0x81, 0x22,
    0x84, 0xdd, 0x1c, 0x02, 0x1c, 0x02, 0x85, 0x1f,
    0xb0, 0xe0, 0xc6, 0x20, 0xb5, 0xe0, 0xb1, 0x20,
    0x83, 0xdf, 0x0c, 0x00
};

// 控制器定义 (Report ID 0x03, 47 bytes)
static const uint8_t ps4_output_0x03[] = {
    0x21, 0x27, 0x04, 0xcf, 0x00, 0x2c, 0x56,
    0x08, 0x00, 0x3d, 0x00, 0xe8, 0x03, 0x04, 0x00,
    0xff, 0x7f, 0x0d, 0x0d, 0x00, 0x00, 0x00, 0x00,
    0x0D, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t ps4_output_0x12[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // device MAC
    0x08, 0x25, 0x00,                      // BT device class
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // host MAC
};

// 版本日期 (Report ID 0xA3, 48 bytes)
static const uint8_t ps4_output_0xa3[] = {
    0x4a, 0x75, 0x6e, 0x20, 0x20, 0x39, 0x20, 0x32,
    0x30, 0x31, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x31, 0x32, 0x3a, 0x33, 0x36, 0x3a, 0x34, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0xb4, 0x01, 0x00, 0x00, 0x00,
    0x07, 0xa0, 0x10, 0x20, 0x00, 0xa0, 0x02, 0x00
};

// 签名状态 - 认证未就绪 (Report ID 0xF2, 15 bytes)
static const uint8_t ps4_output_0xf2_notready[] = {
    0xF2, 0x00, 0x10,  // byte[2] = 16 表示"仍在签名中"
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t ps4_output_0xf3[] = {
    0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00
};

//--------------------------------------------------------------------+
// 设备描述符回调
//--------------------------------------------------------------------+

uint8_t const *tud_descriptor_device_cb(void) {
    return ps4_device_descriptor;  
}

//--------------------------------------------------------------------+
// 配置描述符回调
//--------------------------------------------------------------------+

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index; // for multiple configurations
  return ps4_configuration_descriptor;
}


//--------------------------------------------------------------------+
// 字符串描述符
//--------------------------------------------------------------------+

// String Descriptor Index
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
};

// array of pointer to string descriptors
static char const *string_desc_arr[] = {
    (const char *)ps4_string_language,
    (const char *)ps4_string_manufacturer,
    (const char *)ps4_string_product,
    NULL,
};

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  size_t chr_count;

  switch (index) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL: chr_count = board_usb_get_serial(_desc_str + 1, 32); break;

    default:
      // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
      // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

      if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
        return NULL;
      }

      const char *str = string_desc_arr[index];

      // Cap at max char
      chr_count = strlen(str);

      size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1; // -1 for string type
      if (chr_count > max_count) {
        chr_count = max_count;
      }

      // Convert ASCII string into UTF-16
      for (size_t i = 0; i < chr_count; i++) {
        _desc_str[1 + i] = str[i];
      }
      break;
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}

//--------------------------------------------------------------------+
// HID 报告描述符
//--------------------------------------------------------------------+
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return ps4_report_descriptor;  
}

//--------------------------------------------------------------------+
// HID 回调 - GET_REPORT
//--------------------------------------------------------------------+
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t* buffer, uint16_t reqlen) {
    (void)instance;

    if (report_type == HID_REPORT_TYPE_FEATURE) {
        switch (report_id) {
            case PS4_GET_CALIBRATION: {     // 0x02
                uint16_t len = sizeof(ps4_output_0x02);
                if (reqlen < len) return 0;
                memcpy(buffer, ps4_output_0x02, len);
                return len;
            }
            case PS4_DEFINITION: {          // 0x03
                uint16_t len = sizeof(ps4_output_0x03);
                if (reqlen < len) return 0;
                memcpy(buffer, ps4_output_0x03, len);
                return len;
            }
            case PS4_GET_MAC_ADDRESS: {     // 0x12
                uint16_t len = sizeof(ps4_output_0x12);
                if (reqlen < len) return 0;
                memcpy(buffer, ps4_output_0x12, len);
                return len;
            }
            case PS4_GET_VERSION_DATE: {    // 0xA3
                uint16_t len = sizeof(ps4_output_0xa3);
                if (reqlen < len) return 0;
                memcpy(buffer, ps4_output_0xa3, len);
                return len;
            }
            case PS4_GET_SIGNING_STATE: {   // 0xF2
                uint16_t len = sizeof(ps4_output_0xf2_notready);
                if (reqlen < len) return 0;
                memcpy(buffer, ps4_output_0xf2_notready, len);
                return len;
            }
            case PS4_RESET_AUTH: {          // 0xF3
                uint16_t len = sizeof(ps4_output_0xf3);
                if (reqlen < len) return 0;
                memcpy(buffer, ps4_output_0xf3, len);
                return len;
            }
            case PS4_GET_SIGNATURE_NONCE:   // 0xF1
                return 0;  // STALL
            default:
                break;
        }
    }

    return 0;  // STALL
}

//--------------------------------------------------------------------+
// HID 回调 - SET_REPORT
//--------------------------------------------------------------------+
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const* buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)buffer;
    (void)bufsize;

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        // Report ID 5: LED/震动数据，忽略
    } else if (report_type == HID_REPORT_TYPE_FEATURE) {
        if (report_id == PS4_SET_AUTH_PAYLOAD) {
            // 0xF0: 认证负载，忽略
        }
    }
}

