/********************************** (C) COPYRIGHT  *******************************
* File Name          : msp_protocol.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/30
* Description        : MSP v1 协议常量、命令码、状态码定义
*********************************************************************************/

#ifndef MSP_PROTOCOL_H
#define MSP_PROTOCOL_H

#include <stdint.h>

//--------------------------------------------------------------------+
// 帧格式常量
//--------------------------------------------------------------------+
#define MSP_SYNC1           '$'     // 同步字节1
#define MSP_SYNC2           'M'     // 同步字节2
#define MSP_DIR_REQUEST     '<'     // 方向：请求
#define MSP_DIR_REPLY       '>'     // 方向：成功响应
#define MSP_DIR_ERROR       '!'     // 方向：错误响应

#define MSP_PAYLOAD_MAX     254     // 最大载荷长度（1字节 size 字段）
#define MSP_FRAME_BUF_SIZE  260     // 帧缓冲区大小（size + cmd + payload + crc）

//--------------------------------------------------------------------+
// 命令码
//--------------------------------------------------------------------+

// 基础命令
#define MSP_CMD_API_VERSION     0x01
#define MSP_CMD_FC_VARIANT      0x02
#define MSP_CMD_FC_VERSION      0x03
#define MSP_CMD_REBOOT          0x04

// 配置命令
#define MSP_CMD_CONFIG_GET      0x10
#define MSP_CMD_CONFIG_SET      0x11
#define MSP_CMD_CONFIG_SAVE     0x12
#define MSP_CMD_CONFIG_RESET    0x13
#define MSP_CMD_CONFIG_STATUS   0x14

//摇杆校准命令
#define MSP_CMD_JOYSTICK_RAW    0x15
#define MSP_CMD_CALIB_START     0x16
#define MSP_CMD_CALIB_SAMPLE    0x17
#define MSP_CMD_CALIB_TRACK     0x18
#define MSP_CMD_CALIB_END       0x19
#define MSP_CMD_CALIB_CANCEL    0x1A

//--------------------------------------------------------------------+
// 状态码
//--------------------------------------------------------------------+
#define MSP_STATUS_OK           0   // 成功
#define MSP_STATUS_ERROR        1   // 通用错误
#define MSP_STATUS_INVALID_ID   2   // 无效配置ID
#define MSP_STATUS_INVALID_DATA 3   // 数据长度不正确
#define MSP_STATUS_FLASH_ERROR  4   // Flash写入失败

//--------------------------------------------------------------------+
// 状态机状态
//--------------------------------------------------------------------+
typedef enum {
    MSP_STATE_IDLE = 0,     // 等待 '$'
    MSP_STATE_HEADER_M,     // 等待 'M'
    MSP_STATE_HEADER_DIR,   // 等待 '<'
    MSP_STATE_SIZE,         // 读取 size
    MSP_STATE_CMD,          // 读取 cmd
    MSP_STATE_PAYLOAD,      // 读取 payload
    MSP_STATE_CRC,          // 读取并校验 CRC
} MspState;

//--------------------------------------------------------------------+
// MSP 版本信息
//--------------------------------------------------------------------+
#define MSP_VERSION_MAJOR   0
#define MSP_VERSION_MINOR   1

#define MSP_FC_VARIANT_STR  "P1NKD-DS4"
#define MSP_FC_VERSION_MAJOR 0
#define MSP_FC_VERSION_MINOR 1
#define MSP_FC_VERSION_PATCH 0

//--------------------------------------------------------------------+
// REBOOT 魔术字
//--------------------------------------------------------------------+
#define MSP_REBOOT_MAGIC    0x52    // 'R'

//--------------------------------------------------------------------+
// XOR CRC 计算
//--------------------------------------------------------------------+
static inline uint8_t msp_crc_update(uint8_t crc, uint8_t byte) {
    return crc ^ byte;
}

#endif // MSP_PROTOCOL_H