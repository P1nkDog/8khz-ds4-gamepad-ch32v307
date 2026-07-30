/********************************** (C) COPYRIGHT *******************************
* File Name          : cdc_descriptors.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/29
* Description        : cdc描述符路由层接口声明
********************************************************************************/

#ifndef CDC_DESCRIPTORS_H
#define CDC_DESCRIPTORS_H

#include "tusb.h"

//--------------------------------------------------------------------+
// VID/PID
//--------------------------------------------------------------------+
#define CDC_VENDOR_ID       0x0331
#define CDC_PRODUCT_ID      0x0001

//--------------------------------------------------------------------+
// 端点分配
//--------------------------------------------------------------------+
#define  CDC_EPNUM_NOTIFY   0x81
#define  CDC_EPNUM_OUT      0x02
#define  CDC_EPNUM_IN       0x82

#define CDC_EP_MPS          (TUD_OPT_HIGH_SPEED ? 512 : 64)

//--------------------------------------------------------------------+
// Interface 分配
//--------------------------------------------------------------------+
enum{
    CDC_INTERFACE_NUM = 0,
    CDC_INTERFACE_DATA = 1,
    CDC_INTERFACE_TOTAL = 2,
};

//--------------------------------------------------------------------+
// String Descriptor Index
//--------------------------------------------------------------------+
enum{
    CDC_STRID_LANGID = 0,
    CDC_STRID_MANUFACTURER = 1,
    CDC_STRID_PRODUCT = 2,
    CDC_STRID_SERIAL = 3,
    CDC_STRID_CDC_INTERFACE = 4,
};

//--------------------------------------------------------------------+
// 字符串描述符
//--------------------------------------------------------------------+
static const uint8_t cdc_string_language[]     = { 0x09, 0x04 };
static const uint8_t cdc_string_manufacturer[] = "P1nk";
static const uint8_t cdc_string_product[]      = "DS4 Configurator";
static const uint8_t cdc_string_cdc_interface[] = "DS4 CDC Config";

//--------------------------------------------------------------------+
// Device Descriptor 设备描述符
//--------------------------------------------------------------------+
static const tusb_desc_device_t cdc_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = 64,
    .idVendor           = CDC_VENDOR_ID,
    .idProduct          = CDC_PRODUCT_ID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = CDC_STRID_MANUFACTURER,
    .iProduct           = CDC_STRID_PRODUCT,
    .iSerialNumber      = CDC_STRID_SERIAL,
    .bNumConfigurations = 1
};

//--------------------------------------------------------------------+
// Configuration Descriptor 配置描述符
//--------------------------------------------------------------------+
#define CDC_CONFIG_DESC_LEN  TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN

static const uint8_t cdc_configuration_descriptor[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, CDC_INTERFACE_TOTAL, 0, CDC_CONFIG_DESC_LEN, 0x00, 100),

    // Interface number, string index, EP notification address and size, EP data address (out, in) and size
    TUD_CDC_DESCRIPTOR(CDC_INTERFACE_NUM, CDC_STRID_CDC_INTERFACE,
                        CDC_EPNUM_NOTIFY, 8,
                        CDC_EPNUM_OUT, CDC_EPNUM_IN, CDC_EP_MPS),
};



#endif //CDC_DESCRIPTORS_H