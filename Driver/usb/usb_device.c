/********************************** (C) COPYRIGHT  *******************************
* File Name          : usb_device.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/10/26
* Description        : USB设备栈初始化：角色=Device，速率=本芯片支持的最高速
*********************************************************************************/

#include "tusb.h"
#include "usb_device.h"
#include "usb_sof_jit.h"
#include "device/dcd.h"

//--------------------------------------------------------------------+
// 静态配置
//--------------------------------------------------------------------+

static const tusb_rhport_init_t s_rh_init_device = {
    .role  = TUSB_ROLE_DEVICE,
    .speed = TUD_OPT_HIGH_SPEED ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
};

//--------------------------------------------------------------------+
// api接口
//--------------------------------------------------------------------+

//初始化USB设备栈
bool usb_device_init(void){
    if (!tusb_init(BOARD_TUD_RHPORT, &s_rh_init_device)) {
        return false;
    }

    //SOF JIT 调度器：TIM4 单脉冲时基 + 相位锁定状态机（须在 SOF 中断使能前就绪）
    usb_sof_jit_init();

    //打开 USBHS INT_EN 的 SOF_ACT_EN(bit3)：设备侧 SOF 以 TRANSFER_FLAG + token=SOF 上报
    dcd_sof_enable(BOARD_TUD_RHPORT, true);

    return true;
}
