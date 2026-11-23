/********************************** (C) COPYRIGHT  *******************************
* File Name          : msp_handler.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/28
* Description        : MSP v1 协议状态机 + 命令分发 + 响应构建
*********************************************************************************/

#include <string.h>
#include "tusb.h"
#include "debug.h"
#include "msp_protocol.h"
#include "msp_handler.h"
#include "config.h"
#include "joystick.h"
#include "input.h"
#include "us_timer.h"
#include "ch32v30x.h"

//--------------------------------------------------------------------+
// 编译期帧容量校验
//   MSP_CMD_CONFIG_STATUS 的响应是 [count:1B] + N * [id:1B][size:2B]，
//   每组固定 3 字节。此处锁死"全部配置组能装进单帧"，避免日后把
//   CONFIG_MAX_GROUPS 调大后，响应在运行期悄悄被截断/越界。
//   当前：1 + 64*3 = 193 <= 254，通过。
//--------------------------------------------------------------------+
_Static_assert((1U + (uint32_t)CONFIG_MAX_GROUPS * 3U) <= MSP_PAYLOAD_MAX,
               "CONFIG_MAX_GROUPS exceeds single MSP_CMD_CONFIG_STATUS frame capacity");

//--------------------------------------------------------------------+
// 状态机变量
//--------------------------------------------------------------------+
static MspState  msp_state;
static uint8_t   msp_rx_size;       // 期望的 payload 长度
static uint8_t   msp_rx_cmd;        // 命令码
static uint8_t   msp_rx_buf[MSP_FRAME_BUF_SIZE]; // payload 缓冲区
static uint8_t   msp_rx_offset;     // 已接收的 payload 字节数
static uint8_t   msp_rx_crc;        // 累计 CRC
static uint32_t  msp_last_byte_us;  // 最后一个字节的到达时刻（半包超时判据）

//字节间超时阈值：50ms 远大于任何合法帧的传输时间（260 字节约 23ms @115200 都够），
//又远小于人能感知的"卡住"，超过它就把非 IDLE 状态强制复位。
#define MSP_RX_TIMEOUT_US 50000u

//--------------------------------------------------------------------+
// 发送缓冲区
//--------------------------------------------------------------------+
static uint8_t   msp_tx_buf[MSP_FRAME_BUF_SIZE];

//--------------------------------------------------------------------+
// 初始化
//--------------------------------------------------------------------+
void msp_init(void) {
    msp_state        = MSP_STATE_IDLE;
    msp_rx_offset    = 0;
    msp_rx_size      = 0;
    msp_last_byte_us = micros();
}

//--------------------------------------------------------------------+
// 半包超时复位（由 cdc_process 每轮调用）
//--------------------------------------------------------------------+
//  逐字节状态机只按 size 推进，没有时间维度：主机发半个帧后掉线、页面刷新、
//  或 USB 重枚举，状态就会停在 HEADER_*/PAYLOAD/CRC 里继续"吞"后续字节，
//  直到凑满 size 才回 IDLE。现场表现是"重连后第一条命令必然超时，重发才成功"。
//  这里补上字节间超时：只要超过 MSP_RX_TIMEOUT_US 没有新字节，就丢弃半包。
void msp_poll(void) {
    if (msp_state == MSP_STATE_IDLE) return;

    if ((micros() - msp_last_byte_us) > MSP_RX_TIMEOUT_US) {
        msp_state     = MSP_STATE_IDLE;
        msp_rx_offset = 0;
    }
}

//--------------------------------------------------------------------+
// 发送响应帧
//--------------------------------------------------------------------+

//发送成功响应：$ M > [size] [cmd] [data...] [crc]
//  位置计数器必须是 uint16_t：整帧长度是 len + 6，而 len 最大可达 MSP_PAYLOAD_MAX
//  （254），合计 260 字节 —— 用 uint8_t 会在 len >= 250 时回绕，把 5 字节帧头
//  （$ M > size cmd）踩掉并只发出 4 字节残缺帧。msp_tx_buf 正好是 260 字节，
//  与 len 上限严格匹配。
static void msp_send_reply(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint16_t pos = 0;
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

    //发送：tud_cdc_write() 返回"实际入队字节数"，TX FIFO 满时会少写甚至写 0。
    //  直接丢弃返回值会在线上留下半截帧 —— 上位机按 size 等字节会永久卡住，
    //  比"整帧没发出去"更难排查（后者只会超时重发）。
    //  处理方式：先等 FIFO 腾出整帧空间（CDC 模式没有实时约束，上限约 20ms，
    //  正常路径下 512B FIFO 装 260B 帧一次就够，这个等待不会真的发生）；
    //  再按返回值最多重试 3 轮，每轮之间跑一次 tud_task() 让 USB 栈推进。
    for (uint8_t wait = 0; wait < 20u && tud_cdc_write_available() < pos; wait++) {
        tud_task();
        Delay_Ms(1);
    }

    uint16_t sent = 0;
    for (uint8_t retry = 0; retry < 3u; retry++) {
        sent = (uint16_t)(sent + (uint16_t)tud_cdc_write(msp_tx_buf + sent,
                                                         (uint32_t)(pos - sent)));
        tud_cdc_write_flush();
        if (sent >= pos) return;
        tud_task();
    }
    //三轮仍写不完：放弃本次响应。半截帧已在线上，上位机会按超时重发整条命令。
}

//发送错误响应：$ M > [1] [cmd] [status] [crc]
//  注意：本实现不发送标准 MSP 的 '$ M !' 方向字节，错误帧与成功帧共用 '>'，
//  由 payload 首字节的状态码（MSP_STATUS_*）区分。上位机解析时必须先看状态码，
//  不能只看方向字节。（MSP_DIR_ERROR 因此保留未用，仅作为协议文档常量。）
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
    msp_send_reply(MSP_CMD_FC_VARIANT, (const uint8_t *)MSP_FC_VARIANT_STR, (uint8_t)(sizeof(MSP_FC_VARIANT_STR) - 1));
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

    // 等 CDC 发送缓冲真正排空，而不是空转一个与主频绑定的常数循环：
    //  判据从"过了多久"换成"还有没有字节没发出去"，复位前不会再出现
    //  "ACK 还没上总线设备就重启了"。上限约 20ms，主机在枚举切换 / URB 调度
    //  抖动时偶发 >10ms 的 IN 令牌空档也不会丢 ACK；期间持续跑 tud_task()
    //  让 USB 栈能推进事件。（旧实现是 for(volatile i...) 空转，时长随主频
    //  和优化等级漂移，且与真实发送进度无关。）
    for (uint16_t waited_ms = 0; waited_ms < 20u; waited_ms++) {
        tud_task();
        if (tud_cdc_write_available() >= (uint32_t)CFG_TUD_CDC_TX_BUFSIZE) {
            break;      // FIFO 已空，数据都交给主机了
        }
        Delay_Ms(1);
    }

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

    // 单帧载荷上限保护：响应为 [id:1B][version:1B][data:size]，头部占 2 字节。
    // 超过 252 字节的配置组无法用一帧回传，直接报错，避免下面 memcpy 溢出 resp[]。
    if (desc->size > (MSP_PAYLOAD_MAX - 2U)) {
        msp_send_error(MSP_CMD_CONFIG_GET, MSP_STATUS_INVALID_DATA);
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

    // 数值合法性校验：不合法则直接拒绝，绝不污染 RAM、也不置脏标记，
    // 主机收到 MSP_STATUS_INVALID_DATA(3)。
    // 注意 data 指向 msp_rx_buf 内部（可能是奇地址），config_validate() 的
    // 各回调内部统一"先 memcpy 到局部对齐结构体再读成员"，不存在非对齐加载。
    if (!config_validate(config_id, data)) {
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
    uint8_t pos = 1;      // resp[0] 预留给"实际写入的组数"，循环内同步回填
    uint8_t written = 0;

    resp[0] = 0;

    for (uint8_t i = 0; i < count; i++) {
        const ConfigDesc *desc = config_get_desc_by_index(i);
        if (desc == NULL) break;

        // 单帧载荷边界保护：每组固定写 3 字节，写之前先确认这一组放得下，
        // 放不下就整体报错返回，绝不让 pos 越过 MSP_PAYLOAD_MAX 写爆 resp[]。
        if ((uint16_t)pos + 3U > MSP_PAYLOAD_MAX) {
            msp_send_error(MSP_CMD_CONFIG_STATUS, MSP_STATUS_ERROR);
            return;
        }

        resp[pos++] = (uint8_t)desc->id;
        resp[pos++] = (uint8_t)(desc->size & 0xFF);
        resp[pos++] = (uint8_t)(desc->size >> 8);
        resp[0] = ++written;    // 计数与响应体同步，且不会多于实际条目
    }

    msp_send_reply(MSP_CMD_CONFIG_STATUS, resp, pos);
}


//--------------------------------------------------------------------+
// 命令处理：校准命令
//--------------------------------------------------------------------+

//处理 MSP_CMD_JOYSTICK_RAW：返回 4轴逻辑原始 + 4轴映射后 各u16 LE
static void msp_handle_joystick_raw(void){
    uint8_t resp[17];
    const uint16_t *raw = joystick_get_raw();
    const JoystickOutput *out = joystick_get_output();
    uint16_t out_arr[4] = {out->lx, out->ly, out->rx, out->ry};

    resp[0] = 4;   //轴数量
    uint8_t pos = 1;
    for(uint8_t i = 0; i < 4; i++){
        resp[pos++] = (uint8_t)(raw[i] & 0xFF);
        resp[pos++] = (uint8_t)(raw[i] >> 8);
    }
    for(uint8_t i = 0; i < 4; i++){
        resp[pos++] = (uint8_t)(out_arr[i] & 0xFF);
        resp[pos++] = (uint8_t)(out_arr[i] >> 8);
    }
    msp_send_reply(MSP_CMD_JOYSTICK_RAW, resp, pos);
}

//处理 MSP_CMD_CALIB_START
//  状态机拒绝越阶调用时会返回 false（例如上一次校准还没结束），
//  这里必须把拒绝如实回报给上位机，否则用户会以为已经进入校准。
static void msp_handle_calib_start(void){
    uint8_t status = joystick_calib_start() ? MSP_STATUS_OK : MSP_STATUS_ERROR;
    msp_send_reply(MSP_CMD_CALIB_START, &status, 1);
}

//处理 MSP_CMD_CALIB_SAMPLE：采样中心，返回4轴center
static void msp_handle_calib_sample(void){
    if (!joystick_calib_sample_center()) {
        msp_send_error(MSP_CMD_CALIB_SAMPLE, MSP_STATUS_ERROR);
        return;
    }

    const JoystickCalibConfig *calib = (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
    if (calib == NULL) {
        msp_send_error(MSP_CMD_CALIB_SAMPLE, MSP_STATUS_ERROR);
        return;
    }

    uint8_t resp[9];
    resp[0] = 4;   //轴数量
    for(uint8_t i = 0; i < 4; i++){
        resp[1 + i*2] = (uint8_t)(calib->calib[i].center & 0xFF);
        resp[2 + i*2] = (uint8_t)(calib->calib[i].center >> 8);
    }
    msp_send_reply(MSP_CMD_CALIB_SAMPLE, resp, sizeof(resp));
}

//处理 MSP_CMD_CALIB_TRACK：开始跟踪min/max
static void msp_handle_calib_track(void){
    uint8_t status = joystick_calib_track_start() ? MSP_STATUS_OK : MSP_STATUS_ERROR;
    msp_send_reply(MSP_CMD_CALIB_TRACK, &status, 1);
}

//处理 MSP_CMD_CALIB_END：结束跟踪并保存，返回4轴min/center/max + 状态
static void msp_handle_calib_end(void){
    if(!joystick_calib_track_end()){
        msp_send_error(MSP_CMD_CALIB_END, MSP_STATUS_ERROR);
        return;
    }
    const JoystickCalibConfig *calib = (const JoystickCalibConfig *)config_get(CONFIG_ID_JOYSTICK_CALIB);
    uint8_t resp[26];
    resp[0] = MSP_STATUS_OK;
    resp[1] = 4;   //轴数量
    for(uint8_t i = 0; i < 4; i++){
        uint16_t v[3] = {calib->calib[i].min, calib->calib[i].center, calib->calib[i].max};
        for(uint8_t j = 0; j < 3; j++){
            resp[2 + i*6 + j*2]     = (uint8_t)(v[j] & 0xFF);
            resp[2 + i*6 + j*2 + 1] = (uint8_t)(v[j] >> 8);
        }
    }
    msp_send_reply(MSP_CMD_CALIB_END, resp, sizeof(resp));
}

//处理 MSP_CMD_CALIB_CANCEL：放弃校准
//  已在 IDLE 时"取消"是无效操作，回 ERROR 让上位机知道当前并没有进行中的校准。
static void msp_handle_calib_cancel(void){
    uint8_t status = joystick_calib_cancel() ? MSP_STATUS_OK : MSP_STATUS_ERROR;
    msp_send_reply(MSP_CMD_CALIB_CANCEL, &status, 1);
}


//--------------------------------------------------------------------+
// 命令处理：宏录制快照
//--------------------------------------------------------------------+

//处理 MSP_CMD_INPUT_SNAPSHOT：返回「消抖后按键 + 4 轴摇杆 + 设备时间戳」，固定 16 字节
//  布局（全部小端）：
//    [0..3]  u32 buttons  —— input_read_buttons() 的输出，bit0~25 对应 INPUT_MASK_*
//    [4..5]  u16 lx       —— joystick_get_output()->lx（12-bit，0~4095）
//    [6..7]  u16 ly
//    [8..9]  u16 rx
//    [10..11] u16 ry
//    [12..15] u32 micros() 低 32 位（主机按差值算真实间隔，回绕安全）
//  为什么按键必须在这里自己读：
//    CDC 模式下 main.c 只跑 cdc_process() + joystick_process()，gamepad_process()
//    不运行，gamepad_get_state() 里的按键是上一次 HID 模式的残留值（上电直接进
//    CDC 时全 0），拿它录制会录到一堆"什么都没按"。
//  为什么摇杆不再采样：
//    CDC 主循环每轮都调用 joystick_process()，joystick_get_output() 已经是本轮
//    新算的值；再调一次等于白多采 4 个 ADC 通道（约 37µs）并推动校准跟踪状态机。
static void msp_handle_input_snapshot(void){
    uint32_t buttons = 0;
    input_read_buttons(&buttons);

    const JoystickOutput *out = joystick_get_output();
    const uint32_t ts = micros();

    uint8_t resp[16];
    uint8_t pos = 0;

    resp[pos++] = (uint8_t)(buttons & 0xFF);
    resp[pos++] = (uint8_t)((buttons >> 8) & 0xFF);
    resp[pos++] = (uint8_t)((buttons >> 16) & 0xFF);
    resp[pos++] = (uint8_t)((buttons >> 24) & 0xFF);

    resp[pos++] = (uint8_t)(out->lx & 0xFF);
    resp[pos++] = (uint8_t)(out->lx >> 8);
    resp[pos++] = (uint8_t)(out->ly & 0xFF);
    resp[pos++] = (uint8_t)(out->ly >> 8);
    resp[pos++] = (uint8_t)(out->rx & 0xFF);
    resp[pos++] = (uint8_t)(out->rx >> 8);
    resp[pos++] = (uint8_t)(out->ry & 0xFF);
    resp[pos++] = (uint8_t)(out->ry >> 8);

    resp[pos++] = (uint8_t)(ts & 0xFF);
    resp[pos++] = (uint8_t)((ts >> 8) & 0xFF);
    resp[pos++] = (uint8_t)((ts >> 16) & 0xFF);
    resp[pos++] = (uint8_t)((ts >> 24) & 0xFF);

    msp_send_reply(MSP_CMD_INPUT_SNAPSHOT, resp, pos);
}


//--------------------------------------------------------------------+
// 命令处理：多配置（全局硬件层 + 4 套独立配置）
//--------------------------------------------------------------------+

//0x1C 响应长度：3 + 4 × 32 = 131 字节，必须能装进单帧
_Static_assert((3U + (uint32_t)CONFIG_PROFILE_NAME_MAX * CONFIG_PROFILE_COUNT) <= MSP_PAYLOAD_MAX,
               "MSP_CMD_PROFILE_GET response exceeds single MSP frame capacity");

//处理 MSP_CMD_PROFILE_GET：返回激活配置号 / 总套数 / 盲切开关 / 4 套配置名
//  布局（全部为裸字节，无字节序问题）：
//    [0]        active_profile   当前激活配置 0..3
//    [1]        profile_count    固定 CONFIG_PROFILE_COUNT(4)
//    [2]        shortcut_enable  手柄快捷键盲切总开关 0/1
//    [3..34]    name0            "配置 1" 或用户自定义名（UTF-8，'\0' 结尾）
//    [35..66]   name1
//    [67..98]   name2
//    [99..130]  name3
//  未激活槽位的名字由 config_get_profile_name() 轻量扫描各自扇区得到，
//  不会为了让上位机看到名字而把别套配置加载进 RAM（当前工作集不受影响）。
static void msp_handle_profile_get(const uint8_t *payload, uint8_t len){
    (void)payload;

    //空载荷请求：带载荷说明上位机与固件的协议版本不匹配，直接拒绝
    if (len != 0U) {
        msp_send_error(MSP_CMD_PROFILE_GET, MSP_STATUS_INVALID_DATA);
        return;
    }

    uint8_t resp[3U + (CONFIG_PROFILE_NAME_MAX * CONFIG_PROFILE_COUNT)];
    uint8_t pos = 0;

    resp[pos++] = config_get_active_profile();
    resp[pos++] = (uint8_t)CONFIG_PROFILE_COUNT;
    resp[pos++] = config_is_shortcut_switch_enabled() ? 1U : 0U;

    //4 套名称依次铺满 32 字节定长区（config_get_profile_name 已保证 '\0' 结尾）
    for (uint8_t i = 0; i < (uint8_t)CONFIG_PROFILE_COUNT; i++) {
        config_get_profile_name(i, (char *)&resp[pos]);
        pos = (uint8_t)(pos + CONFIG_PROFILE_NAME_MAX);
    }

    msp_send_reply(MSP_CMD_PROFILE_GET, resp, pos);
}

//处理 MSP_CMD_PROFILE_SWITCH：切换到 payload[0] 指定的配置槽位
//  切换 = 在同一批 RAM 上重放目标槽位的默认值并加载其扇区，不重启、不断开 USB；
//  persist_global = true 会把新的激活号写入全局层 A/B 扇区（掉电记忆）。
//  注意：切换前请先发 CONFIG_SAVE（0x12）保存当前配置的改动——config_switch_profile()
//  不会自动落盘当前套的未保存修改（见 Config/config.c 的接口说明）。
static void msp_handle_profile_switch(const uint8_t *payload, uint8_t len){
    if (len != 1U) {
        msp_send_error(MSP_CMD_PROFILE_SWITCH, MSP_STATUS_INVALID_DATA);
        return;
    }

    if (payload[0] >= (uint8_t)CONFIG_PROFILE_COUNT) {
        msp_send_error(MSP_CMD_PROFILE_SWITCH, MSP_STATUS_INVALID_ID);
        return;
    }

    bool ok = config_switch_profile(payload[0], true);

    uint8_t resp[2];
    resp[0] = config_get_active_profile();
    resp[1] = ok ? (uint8_t)MSP_STATUS_OK : (uint8_t)MSP_STATUS_FLASH_ERROR;

    msp_send_reply(MSP_CMD_PROFILE_SWITCH, resp, sizeof(resp));
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
        case MSP_CMD_JOYSTICK_RAW:  msp_handle_joystick_raw(); break;
        case MSP_CMD_CALIB_START:   msp_handle_calib_start(); break;
        case MSP_CMD_CALIB_SAMPLE:  msp_handle_calib_sample(); break;
        case MSP_CMD_CALIB_TRACK:   msp_handle_calib_track(); break;
        case MSP_CMD_CALIB_END:     msp_handle_calib_end(); break;
        case MSP_CMD_CALIB_CANCEL:  msp_handle_calib_cancel(); break;
        case MSP_CMD_INPUT_SNAPSHOT: msp_handle_input_snapshot(); break;
        case MSP_CMD_PROFILE_GET:   msp_handle_profile_get(payload, len); break;
        case MSP_CMD_PROFILE_SWITCH: msp_handle_profile_switch(payload, len); break;
        default: msp_send_error(cmd, MSP_STATUS_ERROR); break;
    }
}

//--------------------------------------------------------------------+
// 状态机：逐字节处理
//--------------------------------------------------------------------+
void msp_process_byte(uint8_t byte) {
    //每收到一个字节就刷新"最后活动时刻"，msp_poll() 靠它判定半包超时
    msp_last_byte_us = micros();

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
            //载荷长度超限直接丢帧：msp_rx_buf 只能容纳 MSP_PAYLOAD_MAX(254) 字节，
            //  若放行 size > 254，末尾字节会"计入 CRC 但不写入缓冲"，而分发时仍以
            //  size 作为 len 传下去，导致 handler 读到上一帧的残留字节。
            //  在入口就把整帧丢弃，状态机回到 IDLE 等下一个 '$'。
            if (byte > MSP_PAYLOAD_MAX) {
                msp_state = MSP_STATE_IDLE;
                break;
            }
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