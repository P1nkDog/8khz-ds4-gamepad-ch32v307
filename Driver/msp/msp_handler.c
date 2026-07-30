/********************************** (C) COPYRIGHT  *******************************
* File Name          : msp_handler.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/28
* Description        : MSP v1 协议状态机 + 命令分发 + 响应构建
*********************************************************************************/

#include <string.h>
#include "tusb.h"
#include "msp_protocol.h"
#include "msp_handler.h"
#include "config.h"
#include "ch32v30x.h"

//--------------------------------------------------------------------+
// 状态机变量
//--------------------------------------------------------------------+
static MspState  msp_state;
static uint8_t   msp_rx_size;       // 期望的 payload 长度
static uint8_t   msp_rx_cmd;        // 命令码
static uint8_t   msp_rx_buf[MSP_FRAME_BUF_SIZE]; // payload 缓冲区
static uint8_t   msp_rx_offset;     // 已接收的 payload 字节数
static uint8_t   msp_rx_crc;        // 累计 CRC

//--------------------------------------------------------------------+
// 发送缓冲区
//--------------------------------------------------------------------+
static uint8_t   msp_tx_buf[MSP_FRAME_BUF_SIZE];

//--------------------------------------------------------------------+
// 初始化
//--------------------------------------------------------------------+
void msp_init(void) {
    msp_state = MSP_STATE_IDLE;
}

//--------------------------------------------------------------------+
// 发送响应帧
//--------------------------------------------------------------------+

//发送成功响应：$ M > [size] [cmd] [data...] [crc]
static void msp_send_reply(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint8_t pos = 0;
    uint8_t crc = 0;

    // 帧头
    msp_tx_buf[pos++] = MSP_SYNC1;
    msp_tx_buf[pos++] = MSP_SYNC2;
    msp_tx_buf[pos++] = MSP_DIR_REPLY;

    // size
    msp_tx_buf[pos++] = len;
    crc = msp_crc_update(crc, len);

    // cmd
    msp_tx_buf[pos++] = cmd;
    crc = msp_crc_update(crc, cmd);

    // data
    for (uint8_t i = 0; i < len; i++) {
        msp_tx_buf[pos++] = data[i];
        crc = msp_crc_update(crc, data[i]);
    }

    // crc
    msp_tx_buf[pos++] = crc;

    tud_cdc_write(msp_tx_buf, pos);
    tud_cdc_write_flush();
}

//发送错误响应：$ M ! [size] [cmd] [status] [crc]
static void msp_send_error(uint8_t cmd, uint8_t status) {
    msp_send_reply(cmd, &status, 1);
}

//--------------------------------------------------------------------+
// 命令处理：基础命令
//--------------------------------------------------------------------+

//处理 MSP_CMD_API_VERSION
static void msp_handle_api_version(void) {
    uint8_t data[2] = { MSP_VERSION_MAJOR, MSP_VERSION_MINOR };
    msp_send_reply(MSP_CMD_API_VERSION, data, sizeof(data));
}

//处理 MSP_CMD_FC_VARIANT
static void msp_handle_fc_variant(void) {
    msp_send_reply(MSP_CMD_FC_VARIANT, (const uint8_t *)MSP_FC_VARIANT_STR, 4);
}

//处理 MSP_CMD_FC_VERSION
static void msp_handle_fc_version(void) {
    uint8_t data[3] = { MSP_FC_VERSION_MAJOR, MSP_FC_VERSION_MINOR, MSP_FC_VERSION_PATCH };
    msp_send_reply(MSP_CMD_FC_VERSION, data, sizeof(data));
}

//处理 MSP_CMD_REBOOT
static void msp_handle_reboot(const uint8_t *payload, uint8_t len) {
    if (len < 1 || payload[0] != MSP_REBOOT_MAGIC) {
        msp_send_error(MSP_CMD_REBOOT, MSP_STATUS_ERROR);
        return;
    }

    // 先发送响应，再重启
    uint8_t status = MSP_STATUS_OK;
    msp_send_reply(MSP_CMD_REBOOT, &status, 1);

    // 等待 USB 发送完成
    tud_task();
    for (volatile uint32_t i = 0; i < 100000; i++) {}

    // 系统复位
    NVIC_SystemReset();
}

//--------------------------------------------------------------------+
// 命令处理：配置命令
//--------------------------------------------------------------------+

//处理 MSP_CMD_CONFIG_GET
static void msp_handle_config_get(const uint8_t *payload, uint8_t len) {
    if (len < 1) {
        msp_send_error(MSP_CMD_CONFIG_GET, MSP_STATUS_INVALID_DATA);
        return;
    }

    uint16_t config_id = payload[0];
    const ConfigDesc *desc = config_get_desc(config_id);
    if (desc == NULL) {
        msp_send_error(MSP_CMD_CONFIG_GET, MSP_STATUS_INVALID_ID);
        return;
    }

    // 响应格式：[config_id, version, data...]
    uint8_t resp[MSP_PAYLOAD_MAX];
    uint8_t pos = 0;
    resp[pos++] = (uint8_t)desc->id;
    resp[pos++] = desc->version;
    memcpy(&resp[pos], desc->ram, desc->size);
    pos += desc->size;

    msp_send_reply(MSP_CMD_CONFIG_GET, resp, pos);
}

//处理 MSP_CMD_CONFIG_SET
static void msp_handle_config_set(const uint8_t *payload, uint8_t len) {
    if (len < 2) {
        msp_send_error(MSP_CMD_CONFIG_SET, MSP_STATUS_INVALID_DATA);
        return;
    }

    uint16_t config_id = payload[0];
    const uint8_t *data = &payload[1];
    uint8_t data_len = len - 1;

    const ConfigDesc *desc = config_get_desc(config_id);
    if (desc == NULL) {
        msp_send_error(MSP_CMD_CONFIG_SET, MSP_STATUS_INVALID_ID);
        return;
    }

    if (data_len != desc->size) {
        msp_send_error(MSP_CMD_CONFIG_SET, MSP_STATUS_INVALID_DATA);
        return;
    }

    // 写入 RAM 并标记脏
    void *ram = config_get_mutable(config_id);
    memcpy(ram, data, data_len);
    config_mark_dirty(config_id);

    uint8_t resp[2] = { (uint8_t)config_id, MSP_STATUS_OK };
    msp_send_reply(MSP_CMD_CONFIG_SET, resp, sizeof(resp));
}

//处理 MSP_CMD_CONFIG_SAVE
static void msp_handle_config_save(void) {
    bool ok = config_save();
    uint8_t status = ok ? MSP_STATUS_OK : MSP_STATUS_FLASH_ERROR;
    msp_send_reply(MSP_CMD_CONFIG_SAVE, &status, 1);
}

//处理 MSP_CMD_CONFIG_RESET
static void msp_handle_config_reset(const uint8_t *payload, uint8_t len) {
    if (len < 1) {
        msp_send_error(MSP_CMD_CONFIG_RESET, MSP_STATUS_INVALID_DATA);
        return;
    }

    uint16_t config_id = payload[0];
    bool ok = config_reset(config_id);

    if (!ok) {
        msp_send_error(MSP_CMD_CONFIG_RESET, MSP_STATUS_INVALID_ID);
        return;
    }

    uint8_t resp[2] = { (uint8_t)config_id, MSP_STATUS_OK };
    msp_send_reply(MSP_CMD_CONFIG_RESET, resp, sizeof(resp));
}

//处理 MSP_CMD_CONFIG_STATUS
static void msp_handle_config_status(void) {
    uint8_t count = config_get_count();
    uint8_t resp[MSP_PAYLOAD_MAX];
    uint8_t pos = 0;

    resp[pos++] = count;

    for (uint8_t i = 0; i < count; i++) {
        const ConfigDesc *desc = config_get_desc_by_index(i);
        if (desc == NULL) break;
        resp[pos++] = (uint8_t)desc->id;
        resp[pos++] = (uint8_t)(desc->size & 0xFF);
        resp[pos++] = (uint8_t)(desc->size >> 8);
    }

    msp_send_reply(MSP_CMD_CONFIG_STATUS, resp, pos);
}

//--------------------------------------------------------------------+
// 命令分发
//--------------------------------------------------------------------+
static void msp_dispatch(uint8_t cmd, const uint8_t *payload, uint8_t len) {
    switch (cmd) {
        case MSP_CMD_API_VERSION:   msp_handle_api_version(); break;
        case MSP_CMD_FC_VARIANT:    msp_handle_fc_variant(); break;
        case MSP_CMD_FC_VERSION:    msp_handle_fc_version(); break;
        case MSP_CMD_REBOOT:        msp_handle_reboot(payload, len); break;
        case MSP_CMD_CONFIG_GET:    msp_handle_config_get(payload, len); break;
        case MSP_CMD_CONFIG_SET:    msp_handle_config_set(payload, len); break;
        case MSP_CMD_CONFIG_SAVE:   msp_handle_config_save(); break;
        case MSP_CMD_CONFIG_RESET:  msp_handle_config_reset(payload, len); break;
        case MSP_CMD_CONFIG_STATUS: msp_handle_config_status(); break;
        default: msp_send_error(cmd, MSP_STATUS_ERROR); break;
    }
}

//--------------------------------------------------------------------+
// 状态机：逐字节处理
//--------------------------------------------------------------------+
void msp_process_byte(uint8_t byte) {
    switch (msp_state) {
        case MSP_STATE_IDLE:
            if (byte == MSP_SYNC1) {
                msp_state = MSP_STATE_HEADER_M;
            }
            break;

        case MSP_STATE_HEADER_M:
            if (byte == MSP_SYNC2) {
                msp_state = MSP_STATE_HEADER_DIR;
            } else {
                msp_state = MSP_STATE_IDLE;
            }
            break;

        case MSP_STATE_HEADER_DIR:
            if (byte == MSP_DIR_REQUEST) {
                msp_state = MSP_STATE_SIZE;
                msp_rx_crc = 0;
            } else {
                msp_state = MSP_STATE_IDLE;
            }
            break;

        case MSP_STATE_SIZE:
            msp_rx_size = byte;
            msp_rx_crc = msp_crc_update(msp_rx_crc, byte);
            msp_rx_offset = 0;
            msp_state = MSP_STATE_CMD;
            break;

        case MSP_STATE_CMD:
            msp_rx_cmd = byte;
            msp_rx_crc = msp_crc_update(msp_rx_crc, byte);
            if (msp_rx_size > 0) {
                msp_state = MSP_STATE_PAYLOAD;
            } else {
                msp_state = MSP_STATE_CRC;
            }
            break;

        case MSP_STATE_PAYLOAD:
            if (msp_rx_offset < MSP_PAYLOAD_MAX) {
                msp_rx_buf[msp_rx_offset] = byte;
            }
            msp_rx_crc = msp_crc_update(msp_rx_crc, byte);
            msp_rx_offset++;
            if (msp_rx_offset >= msp_rx_size) {
                msp_state = MSP_STATE_CRC;
            }
            break;

        case MSP_STATE_CRC:
            if (byte == msp_rx_crc) {
                // CRC 校验通过，分发命令
                msp_dispatch(msp_rx_cmd, msp_rx_buf, msp_rx_size);
            }
            // CRC 失败则静默丢弃
            msp_state = MSP_STATE_IDLE;
            break;

        default:
            msp_state = MSP_STATE_IDLE;
            break;
    }
}