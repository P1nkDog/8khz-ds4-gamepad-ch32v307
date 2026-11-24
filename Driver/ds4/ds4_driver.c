/********************************** (C) COPYRIGHT  *******************************
* File Name          : ds4_driver.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/07/01
* Description        : ds4驱动文件
*********************************************************************************/

#include <string.h>
#include <stddef.h>

#include "board_api.h"
#include "tusb.h"
#include "ds4_descriptors.h"
#include "ds4_driver.h"
#include "us_timer.h"
#include "config.h"

//--------------------------------------------------------------------+
// 报告布局编译期断言（报告描述符声明 63 字节 + Report ID = 64）
// 注意：leftTrigger/rightTrigger 是 8 位位域，不能对它们取 offsetof，
//       因此用非位域成员的偏移间接锁定：reportID@0、rightStickY@4、miscData@10
//       三者成立即证明 dpad+按键+counter 恰好占 3 字节、扳机占 2 字节。
//--------------------------------------------------------------------+
_Static_assert(sizeof(PS4Report) == 64, "PS4Report 必须恰好 64 字节");
_Static_assert(offsetof(PS4Report, reportID)    == 0,  "报告布局漂移(报告ID)");
_Static_assert(offsetof(PS4Report, rightStickY) == 4,  "报告布局漂移(摇杆)");
_Static_assert(offsetof(PS4Report, miscData)    == 10, "报告布局漂移(vendor区)");

//--------------------------------------------------------------------+
// 发送状态（事件驱动，主循环与回调之间共享）
//--------------------------------------------------------------------+
static volatile bool     s_ep_free      = true;   // 应用层"上一帧已完成"信用
static volatile uint32_t s_drop_count   = 0;      // 累计丢帧（端点忙 / 提交失败）
static volatile uint32_t s_stall_count  = 0;      // 完成回调迟到次数（诊断用，不计入丢帧）
static uint32_t          s_ep_busy_us   = 0;      // 端点开始忙的时刻
static uint32_t          s_ep_timeout_us = 1000u; // 本次传输的看门狗阈值（按档位动态算）

//--------------------------------------------------------------------+
// 发送状态查询与空闲超时兜底
//--------------------------------------------------------------------+

//看门狗倍数与上下限
//  高速端点发出后要等主机下一次 IN 令牌才算完成，物理上至少忙满一个帧周期
//  （250Hz→4ms、1kHz→1ms、8kHz→125us）。倍数取 6：既给完成事件在 tud_task
//  队列里排队留出余量，又不至于让一帧卡死后要等太久才记账。
//  下限 500us：8kHz 档 6×125us 只有 750us，再往下压会接近"一次 ISR + 一次组帧"
//  的抖动范围，容易把正常帧误记成迟到；上限 32ms：250Hz 档 6×4ms = 24ms，
//  封顶是为了防止将来配置更低档位时把阈值拉爆。
#define DS4_WATCHDOG_PERIOD_MULT   6u
#define DS4_WATCHDOG_MIN_US        500u
#define DS4_WATCHDOG_MAX_US        32000u

//计算本次传输的看门狗阈值（跟随当前轮询档位，125us ~ 4000us → 750us ~ 24ms）
static uint32_t ds4_watchdog_timeout_us(void) {
    uint32_t period  = config_get_poll_interval_us();
    uint32_t timeout = period * DS4_WATCHDOG_PERIOD_MULT;

    if (timeout < DS4_WATCHDOG_MIN_US) timeout = DS4_WATCHDOG_MIN_US;
    if (timeout > DS4_WATCHDOG_MAX_US) timeout = DS4_WATCHDOG_MAX_US;
    return timeout;
}

//端点是否可发送
//  1) tud_hid_ready() 是唯一权威判据：它内部是 tud_ready()（= tud_mounted() &&
//     !tud_suspended()）&& ep_in != 0 && !usbd_edpt_busy()。未挂载、总线挂起、
//     端点仍然忙这三种情况一律"本帧不发"，且**不**计入丢帧——这些状态下压根
//     没有"本该发出去却被丢掉"的帧。旧实现在挂起时靠看门狗每 1ms 强放一次，
//     结果每帧都在 usbd_edpt_claim() 上失败，把 s_drop_count 刷成噪音。
//  2) s_ep_free 是应用层信用，只用来识别"完成回调没回来"这一种异常，不再作为
//     放行开关：TinyUSB 单缓冲端点忙时强行重入必然 claim 失败。
bool ds4_ready_to_send(void) {
    if (!tud_hid_ready()) return false;

    if (s_ep_free) return true;

    //硬件端点已经空闲（上一步已确认 !usbd_edpt_busy），但信用还没归还：
    //  只可能是完成事件被 ClearHalt / 总线复位打断——usbd 清了 BUSY 却没有回调
    //  tud_hid_report_complete_cb()。这里只把信用补回来并记一次诊断，
    //  **不在本帧发送**，避免同一令牌周期内重复组帧；下一次调用即恢复正常。
    if ((micros() - s_ep_busy_us) >= s_ep_timeout_us) {
        s_ep_free    = true;
        s_stall_count++;
        return false;
    }

    return false;
}

//累计丢帧数（端点忙 / 提交失败，属于真实丢失）
uint32_t ds4_get_drop_count(void) {
    return s_drop_count;
}

//累计"完成回调迟到"次数（诊断用，不计入丢帧）
uint32_t ds4_get_stall_count(void) {
    return s_stall_count;
}

//--------------------------------------------------------------------+
// 手柄数据发送（非阻塞）
//--------------------------------------------------------------------+
bool ds4_send_report(const PS4Report *report) {
    if (!s_ep_free) {
        s_drop_count++;
        return false;
    }

    //先置忙、再提交：tud_hid_report_complete_cb() 跑在 USB 中断上下文，
    //  若"先提交后置忙"，理论上存在"回调先把 s_ep_free 置 true、随后又被
    //  这里的赋值覆盖成 false"的窗口，端点会一直挂到看门狗超时才解锁。
    //  先置忙后提交后，ISR 只可能把忙改成空闲，状态变化方向永远是安全的。
    s_ep_busy_us    = micros();
    s_ep_timeout_us = ds4_watchdog_timeout_us();
    s_ep_free       = false;

    if (!tud_hid_report(0, report, sizeof(PS4Report))) {
        s_ep_free = true;      //提交失败，立刻回滚忙标志，不占着看门狗窗口
        s_drop_count++;
        return false;
    }
    return true;
}

//TinyUSB IN 传输完成回调：一个令牌回一帧
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    (void)instance; (void)report; (void)len;
    s_ep_free = true;
}

//--------------------------------------------------------------------+
// 主循环处理
//--------------------------------------------------------------------+
void ds4_process(const GamepadState *state) {

    PS4Report report;
    memset(&report, 0, sizeof(PS4Report));
    report.reportID = 0x01;

    // 上报计数器：主机侧连续无跳号 = 一帧未丢（8kHz 的地面真值）
    static uint8_t s_report_counter = 0;
    report.reportCounter = (uint8_t)(s_report_counter & 0x3Fu);
    s_report_counter = (uint8_t)((s_report_counter + 1u) & 0x3Fu);

    report.leftStickX = state -> lx >> 4;
    report.leftStickY = state -> ly >> 4;
    report.rightStickX = state -> rx >> 4;
    report.rightStickY = state -> ry >> 4;

    report.dpad = state->dpad;

    report.buttonWest     = state->buttonWest;
    report.buttonSouth    = state->buttonSouth;
    report.buttonEast     = state->buttonEast;
    report.buttonNorth    = state->buttonNorth;
    report.buttonL1       = state->buttonL1;
    report.buttonR1       = state->buttonR1;
    report.buttonL2       = state->buttonL2;
    report.buttonR2       = state->buttonR2;
    report.buttonSelect   = state->buttonSelect;
    report.buttonStart    = state->buttonStart;
    report.buttonL3       = state->buttonL3;
    report.buttonR3       = state->buttonR3;
    report.buttonHome     = state->buttonHome;
    report.buttonTouchpad = state->buttonTouchpad;

    report.leftTrigger    = state->buttonL2 ? 0xFF : 0x00;
    report.rightTrigger   = state->buttonR2 ? 0xFF : 0x00;

    report.miscData[20] = 0x1B;

    ds4_send_report(&report);
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

static const uint8_t ps4_output_0x12[] = {
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

