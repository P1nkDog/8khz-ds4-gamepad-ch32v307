# CH32V307VCT TinyUSB DS4 Gamepad Project

## 项目概述

| 项目 | 说明 |
|------|------|
| 项目名称 | CH32V307VCT_tinyusb_ds4_A |
| 目标芯片 | CH32V307VCT (WCH 沁恒微电子) |
| CPU 架构 | RISC-V RV32IMACXW |
| IDE | MounRiver Studio 2.5.0 (Eclipse CDT) |
| 编译器 | riscv-none-embed-gcc (GNU MCU RISC-V GCC) |
| USB 协议栈 | TinyUSB (子模块, v0.20.0) |
| 目标功能 | PS4 DualShock 4 (DS4) 游戏手柄 USB HID 设备 |
| 当前状态 | **模板阶段** — main.c 尚未实现任何 USB/手柄功能 |

---

## 硬件平台

### CH32V307VCT 芯片参数

- **内核**: 青稞 RISC-V4C, RV32IMACXW
- **主频**: 144MHz (外部 8MHz HSE 晶振 × PLL 18 倍频)
- **Flash**: 288KB (链接脚本配置)
- **SRAM**: 32KB (链接脚本配置)
- **USB**: 内置 USBHS (High-Speed) 和 USBFS (Full-Speed) 控制器
- **外设**: ADC, DAC, CAN, SPI, I2C, USART, TIM, DMA, ETH, SDIO, GPIO 等

### 内存布局 (Link.ld)

```
FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 288K
RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 32K
Stack       : 2048 bytes (位于 RAM 末尾)
```

CH32V307 系列支持多种 FLASH/RAM 组合（如 256K+64K, 192K+128K 等），本项目选用 **288K FLASH + 32K RAM** 配置。

---

## 项目目录结构

```
CH32V307VCT_tinyusb_ds4_A/
├── Core/                   # RISC-V 核心支持
│   ├── core_riscv.c        # CSR 寄存器读写函数
│   └── core_riscv.h        # PFIC/SysTick 结构体, NVIC 内联函数
├── Debug/                  # 调试工具
│   ├── debug.c             # 延迟函数, USART printf 重定向, _sbrk 堆管理
│   └── debug.h             # 调试配置 (默认 UART1)
├── Ld/                     # 链接脚本
│   └── Link.ld             # 288K FLASH + 32K RAM 布局
├── Peripheral/             # CH32V30x 标准外设库
│   ├── inc/                # 外设头文件 (28个)
│   └── src/                # 外设源文件 (28个)
├── Startup/                # 启动文件
│   ├── startup_ch32v30x_D8C.S  # D8C 系列启动汇编 (本项目使用)
│   └── startup_ch32v30x_D8.S   # D8 系列启动汇编 (未使用)
├── TinyUSB/                # TinyUSB 子模块 (v0.20.0)
│   └── src/
│       ├── class/          # USB 设备类 (HID, CDC, MSC, MIDI 等)
│       ├── common/         # 公共模块 (tusb_fifo)
│       ├── device/         # USB 设备核心 (usbd, usbd_control)
│       ├── host/           # USB 主机核心 (usbh, hub)
│       ├── portable/       # 平台移植层
│       │   └── wch/        # WCH CH32 USB 驱动 (本项目关键)
│       └── typec/          # USB Type-C
├── User/                   # 用户代码
│   ├── main.c              # 主程序入口
│   ├── ch32v30x_it.c       # 中断服务函数
│   ├── ch32v30x_it.h       # 中断头文件
│   ├── ch32v30x_conf.h     # 外设库配置
│   ├── system_ch32v30x.c   # 系统时钟初始化
│   ├── system_ch32v30x.h   # 系统时钟头文件
│   └── tusb_config.h       # TinyUSB 配置
├── obj/                    # 编译输出
│   ├── CH32V307VCT_tinyusb_ds4_A.elf
│   ├── CH32V307VCT_tinyusb_ds4_A.hex
│   ├── CH32V307VCT_tinyusb_ds4_A.map
│   └── ...                 # .o 目标文件, makefile 等
├── .cproject               # Eclipse CDT 项目配置
├── .project                # Eclipse 项目文件
├── .template               # MRS 模板信息
├── .gitmodules             # Git 子模块 (TinyUSB)
└── CH32V307VCT_tinyusb_ds4_A.launch  # 调试启动配置
```

---

## 编译配置

### 编译器标志

| 选项 | 说明 |
|------|------|
| `-march=rv32imacxw` | RISC-V 架构: 基础整数 + 乘法 + 原子 + 压缩 + WCH 扩展 |
| `-mabi=ilp32` | 32位整数 ABI |
| `-Os` | 优化等级: 按大小优化 |
| `-msave-restore` | 使用小序言/结语 |
| `-msmall-data-limit=8` | 小数据段限制 8 字节 |
| `-ffunction-sections` | 函数独立段 |
| `-fdata-sections` | 数据独立段 |
| `-fno-common` | 禁止未初始化公共变量 |
| `-fsigned-char` | char 默认有符号 |
| `-nostartfiles` | 不使用标准启动文件 (使用自定义 startup.S) |
| `--specs=nano.specs` | 使用 newlib-nano |
| `--specs=nosys.specs` | 不使用系统调用 |
| `--gc-sections` | 链接时去除未使用段 |
| `-T Link.ld` | 使用自定义链接脚本 |
| 链接 `-lm` | 链接数学库 |

### Include 路径

```
Debug/
Core/
User/
Peripheral/inc/
TinyUSB/src/
```

### 预定义符号

- `CH32V30x_D8C` — 芯片系列标识 (通过 .cproject 配置)

---

## 被编译的源文件清单

### 用户代码 (3个 .c)

| 文件 | 编译产物 | 说明 |
|------|----------|------|
| `User/main.c` | `User/main.o` | 主程序 — 目前为 WCH 模板, 初始化时钟/USART, 打印 ChipID, 空 while 循环 |
| `User/ch32v30x_it.c` | `User/ch32v30x_it.o` | 中断处理 — NMI 死循环, HardFault 调用 NVIC_SystemReset |
| `User/system_ch32v30x.c` | `User/system_ch32v30x.o` | 系统时钟 — 选择 `SYSCLK_FREQ_144MHz_HSE` (8MHz × 18 = 144MHz) |

### 调试与核心 (2个 .c)

| 文件 | 编译产物 | 说明 |
|------|----------|------|
| `Debug/debug.c` | `Debug/debug.o` | Delay_Us/Ms (SysTick), USART_Printf_Init (USART1 PA9), _write (printf), _sbrk (堆) |
| `Core/core_riscv.c` | `Core/core_riscv.o` | RISC-V CSR 读写: FFLAGS, FCSR, MSTATUS, MTVEC, MEPC, MCAUSE 等 |

### 启动文件 (1个 .S)

| 文件 | 编译产物 | 说明 |
|------|----------|------|
| `Startup/startup_ch32v30x_D8C.S` | `Startup/startup_ch32v30x_D8C.o` | 向量表 (80+ 中断), handle_reset (GP/SP 初始化, .data 拷贝, .bss 清零, 流水线配置, 中断嵌套使能, 调用 SystemInit 跳转 main) |

### CH32V30x 外设库 (28个 .c)

全部外设驱动均被编译 (由 Eclipse CDT 管理, 通过 --gc-sections 链接时自动去除未引用的函数):

`ch32v30x_adc`, `ch32v30x_bkp`, `ch32v30x_can`, `ch32v30x_crc`, `ch32v30x_dac`, `ch32v30x_dbgmcu`, `ch32v30x_dma`, `ch32v30x_dvp`, `ch32v30x_eth`, `ch32v30x_exti`, `ch32v30x_flash`, `ch32v30x_fsmc`, `ch32v30x_gpio`, `ch32v30x_i2c`, `ch32v30x_iwdg`, `ch32v30x_misc`, `ch32v30x_opa`, `ch32v30x_pwr`, `ch32v30x_rcc`, `ch32v30x_rng`, `ch32v30x_rtc`, `ch32v30x_sdio`, `ch32v30x_spi`, `ch32v30x_tim`, `ch32v30x_usart`, `ch32v30x_wwdg`

### TinyUSB 库 (约70个 .c)

所有 TinyUSB 设备/主机/类驱动均被编译, 包括:

**核心:**
- `tusb.c`, `tusb_fifo.c`, `usbd.c`, `usbd_control.c`, `usbh.c`, `hub.c`, `usbc.c`

**设备类:**
- `audio_device`, `bth_device`, `cdc_device`, `cdc_host`, `cdc_rndis_host`, `dfu_device`, `dfu_rt_device`, `hid_device`, `hid_host`, `midi_device`, `midi_host`, `msc_device`, `msc_host`, `mtp_device`, `ecm_rndis_device`, `ncm_device`, `usbtmc_device`, `vendor_device`, `vendor_host`, `video_device`

**平台移植 (WCH 关键驱动):**
- `dcd_ch32_usbfs.c` — CH32 USB Full-Speed 设备控制器驱动
- `dcd_ch32_usbhs.c` — CH32 USB High-Speed 设备控制器驱动
- `hcd_ch32_usbfs.c` — CH32 USB FS 主机控制器驱动

**其他平台移植 (均被编译但不会被链接使用):**
- STM32, NXP, Raspberry Pi RP2040, Nordic nRF5x, Microchip SAM/PIC, Renesas, Sony, Dialog, Synopsys DWC2, EHCI, OHCI 等

---

## 启动流程

```
上电复位
  │
  ▼
_start (startup_ch32v30x_D8C.S)
  │  跳转 handle_reset
  ▼
handle_reset
  │  1. 初始化 GP (全局指针, __global_pointer$)
  │  2. 初始化 SP (栈指针, _eusrstack)
  │  3. 拷贝 .data 从 FLASH 到 RAM
  │  4. 清零 .bss 段
  │  5. 配置流水线和指令预测 (csr 0xbc0)
  │  6. 使能中断嵌套和硬件栈 (csr 0x804)
  │  7. 使能浮点和全局中断, 配置特权模式 (mstatus = 0x6088)
  │  8. 配置中断向量表 (mtvec = _vector_base | 3)
  │  9. 调用 SystemInit()
  │  10. 写 mepc = main, mret 跳转
  ▼
SystemInit (system_ch32v30x.c)
  │  1. 复位 RCC 时钟配置
  │  2. 调用 SetSysClock()
  ▼
SetSysClockTo144_HSE
  │  1. 使能 HSE (外部 8MHz 晶振)
  │  2. 等待 HSE 就绪
  │  3. 配置 AHB/APB 分频 (HCLK=SYSCLK, PCLK2=HCLK, PCLK1=HCLK/2)
  │  4. 配置 PLL: HSE × 18 = 144MHz
  │  5. 使能 PLL, 等待就绪
  │  6. 切换系统时钟到 PLL
  ▼
main (main.c)
  │  1. NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2)
  │  2. SystemCoreClockUpdate()
  │  3. Delay_Init()
  │  4. USART_Printf_Init(115200) — USART1 PA9
  │  5. printf 打印 SystemClk, ChipID
  │  6. while(1) { } — 空循环
  ▼
(等待 USB 功能实现)
```

---

## 中断向量表

启动文件定义了完整的中断向量表 (共 80+ 中断源), 所有中断处理函数声明为 `weak`, 默认指向死循环。用户可以在 `ch32v30x_it.c` 中覆盖这些函数。

关键中断:

| 中断名 | 编号 | 说明 |
|--------|------|------|
| `NMI_Handler` | 2 | 不可屏蔽中断 |
| `HardFault_Handler` | 3 | 硬件错误 (已覆盖: 调用 NVIC_SystemReset) |
| `SysTick_Handler` | 12 | 系统滴答定时器 |
| `USBFS_IRQHandler` | 66 | USB Full-Speed 中断 |
| `USBHSWakeup_IRQHandler` | 67 | USB HS 唤醒中断 |
| `USBHS_IRQHandler` | 68 | USB High-Speed 中断 |

---

## TinyUSB 配置 (tusb_config.h)

```c
#define CFG_TUSB_MCU          OPT_MCU_CH32V307    // 目标 MCU
#define CFG_TUSB_OS           OPT_OS_NONE          // 无 RTOS
#define CFG_TUSB_DEBUG        0                    // 调试关闭
#define CFG_TUD_ENABLED       1                    // 设备模式使能
#define CFG_TUD_MAX_SPEED     OPT_MODE_DEFAULT_SPEED // 默认最高速度
#define CFG_TUD_ENDPOINT0_SIZE 64                  // EP0 包大小

// 设备类配置
#define CFG_TUD_HID           1                    // ★ HID 使能
#define CFG_TUD_CDC           0                    // CDC 关闭
#define CFG_TUD_MSC           0                    // MSC 关闭
#define CFG_TUD_MIDI          0                    // MIDI 关闭
#define CFG_TUD_VENDOR        0                    // Vendor 关闭
#define CFG_TUD_HID_EP_BUFSIZE 64                  // HID 端点缓冲区
```

---

## USB 驱动架构 (TinyUSB WCH 移植)

### USBHS 设备控制器驱动 (dcd_ch32_usbhs.c)

- **端点数量**: 16 个双向端点 (EP0~EP15)
- **DMA**: 已启用 (`USBHS_DMA_EN`)
- **速度**: High-Speed (480Mbps)
- **EP0 缓冲**: 独立 64 字节缓冲区
- **EP1~15**: 使用 DMA 地址直接访问用户缓冲区
- **中断处理**: SETUP_ACT, TRANSFER, BUS_RST, SUSPEND, ISO_ACT

### 数据传输流程

```
dcd_edpt_xfer() 设置传输参数
  │
  ▼
xfer_data_packet() 准备数据包
  │  IN:  填充 TX 缓冲/DMA 地址, 设置 TX_LEN
  │  OUT: 设置 RX DMA 地址, RX_MAX_LEN
  ▼
ep_set_response_and_toggle() 设置 ACK/NAK
  │
  ▼
硬件传输 → 中断触发
  │
  ▼
dcd_int_handler() 处理中断
  │  TRANSFER: 检查 token (IN/OUT), 更新 queued_len
  │  SETUP:    收到 SETUP 包
  │  BUS_RST:  总线复位, 重置为高速模式
  │  SUSPEND:  挂起事件
  ▼
dcd_event_xfer_complete() / dcd_event_setup_received()
  │  通知 TinyUSB 核心
```

---

## 当前项目状态

### 已完成

- [x] 工程创建 (MounRiver Studio)
- [x] 系统时钟配置 (144MHz HSE)
- [x] TinyUSB 子模块集成 (v0.20.0)
- [x] TinyUSB 配置 (tusb_config.h, 仅 HID 使能)
- [x] WCH USBHS 设备驱动集成
- [x] 编译通过, 生成 .elf/.hex
- [x] USART1 调试输出 (PA9, 115200bps)

### 待实现

- [ ] USB HID 设备描述符 (PS4 手柄)
- [ ] USB HID 报告描述符
- [ ] PS4 手柄协议实现 (认证/加密)
- [ ] 按键/摇杆数据采集
- [ ] USB 中断回调注册 (USBHS_IRQHandler → dcd_int_handler)
- [ ] TinyUSB 任务循环 (tud_task)
- [ ] 手柄 LED/震动马达反馈

---

## 编译与烧录

### 编译

在 MounRiver Studio 中点击 Build (或按 Ctrl+B), 输出到 `obj/` 目录。

### 烧录

使用 WCH-Link 调试器, 通过 MounRiver Studio 的 Debug 配置 (`CH32V307VCT_tinyusb_ds4_A.launch`) 烧录。

### 调试串口

USART1, PA9 引脚, 115200bps, 8N1。上电后输出:

```
SystemClk:144000000
ChipID:xxxxxxxx
This is printf example
```

---

## 依赖关系

```
main.c
  └─ debug.h
       └─ ch32v30x.h (芯片总头文件)
            └─ core_riscv.h
            └─ ch32v30x_conf.h
                 └─ ch32v30x_*.h (全部外设头文件)

tusb_config.h (独立配置文件, 被 TinyUSB 源码包含)

TinyUSB/src/portable/wch/dcd_ch32_usbhs.c
  └─ ch32_usbhs_reg.h
  └─ device/dcd.h (TinyUSB 设备控制器接口)
```

---

## 参考资料

- [CH32V307 参考手册](https://www.wch.cn/products/CH32V307.html)
- [TinyUSB 官方文档](https://docs.tinyusb.org/)
- [TinyUSB GitHub](https://github.com/hathach/tinyusb) (子模块 v0.20.0)
- [MounRiver Studio](http://www.mounriver.com/)
- [WCH 官网](https://www.wch.cn/)
