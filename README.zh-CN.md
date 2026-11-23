# CH32V307 DS4 8K 手柄 — v0.2.0

[English](README.md) | [简体中文](README.zh-CN.md)

基于 **WCH CH32V307VCT6**（RISC-V RV32IMACXW @ 144 MHz）与 **TinyUSB 0.21** 的
高速 USB 2.0（480 Mbps）**PS4 DualShock 4 兼容手柄**固件。采用芯片原生高速口，
HID 中断端点最小可到 `bInterval = 1`，即 **125 µs 一帧（8 kHz 回报率）**。

完整方案包括：固件（本仓库）+ PCB（`PCB/v0.2.0`）+ 网页配置器。

配套网页配置器：[P1nkDog/ds4-gamepad-configurator](https://github.com/P1nkDog/ds4-gamepad-configurator)

- **固件版本**：0.1.0（可通过 MSP 命令查询）
- **硬件版本**：v0.2.0（**本文档的引脚表是唯一标准**）

> ⚠️ **本文档第 2 节的引脚映射对应 v0.2.0 正式版 PCB，已上板实测确认。**
> `Docs/` 目录下的旧文档（例如 `引脚重映射修改指南_2026-10-10.md`）仅作历史归档保留，
> 其引脚/极性与实际硬件**冲突**，请勿再按旧文档接线或改代码。

---

## 1. 项目简介与核心特性

| 特性 | 说明 |
|---|---|
| 回报率 | 250 / 500 / 1000 / 2000 / 4000 / 8000 Hz 六档，可在网页里切换（默认 **1000 Hz**）。实现方式是改 HID 描述符的 `bInterval`（`6 - 档位`），8 kHz 即 125 µs 一帧 |
| 原生 DS4 身份 | 报告描述符与 64 字节输入报告严格按 DualShock 4 规范实现，主机识别为 `054C:09CC` DualShock 4，无需驱动、无需映射软件 |
| 双模式启动 | 直接插线 → **DS4 HID 手柄模式**；按住 **Share(PB12)** 插线 → **CDC 配置模式**（虚拟串口，给网页工具用） |
| 摇杆处理 | 12-bit ADC 采样，四轴独立 `min / center / max` 校准，死区 0~50%，死区外满量程映射，参数存 Flash |
| 参数存储 | 两个 4 KB Flash 扇区（`0x0803E000` / `0x0803F000`），magic `0xCF07` + CRC16-CCITT，先写备份区 B 再提交主区 A，写入过程掉电始终有一份完整数据 |
| MSP v1 协议 | 15 条命令：版本查询、配置读/写/保存/复位/状态、摇杆实时值、五步校准、设备重启 |
| 确定性发送 | 发送完全由主机令牌驱动（端点完成回调解锁），没有软件节拍循环，不存在报文撕裂 |
| 诊断能力 | 可选的代码段耗时统计（`Driver/stat`，默认编译为空），丢帧计数可经 MSP/串口读出 |

---

## 2. 硬件规格与引脚映射

### 2.1 MCU 与关键器件

| 项目 | 参数 |
|---|---|
| MCU | CH32V307VCT6 — RISC-V 青稞 V4F，RV32IMACXW，144 MHz，288 KB Flash / 32 KB RAM（LQFP-100） |
| USB | USB 2.0 高速设备（480 Mbps），片内高速 PHY，`bcdUSB = 0x0200` |
| 摇杆 | 双摇杆，ADC1 轮询采样（未用 DMA） |
| 按键 | 主板轻触开关 + FPC 按键板（二选一/并联，MCU 侧引脚相同）；另有 1 个 BOOT0 拨动开关 |
| 存储 | 片内 Flash，划出 2 × 4 KB 扇区存放参数 |
| LED | **无**。v0.2.0 主板没有板载用户 LED，`board_led_write()` 是刻意的空实现 |

### 2.2 时钟树

| 时钟域 | 频率 | 说明 |
|---|---|---|
| HSE | 8 MHz | 外部晶振 |
| SYSCLK / HCLK | 144 MHz | PLL = HSE × 18 |
| PCLK1（APB1） | 72 MHz | HCLK / 2 —— USART2（调试串口）、TIM2（×2 = 144 MHz 定时器时钟） |
| PCLK2（APB2） | 72 MHz | HCLK / 2 —— 刻意降频，使 **ADC = 72 / 8 = 9 MHz**，严格落在数据手册 14 MHz 上限之内 |
| USBHS | 480 MHz | USBHS PLL 参考时钟 = HSE/2 = 4 MHz |
| ADC1 | 9 MHz | `RCC_ADCCLKConfig(RCC_PCLK2_Div8)`；71.5 + 12.5 = 84 个 ADC 周期 ≈ **9.3 µs/通道**，四通道约 37 µs/帧 |

> 为什么 PCLK2 不跑 144 MHz：ADC 时钟只能由 PCLK2 做 /2…/8 分频（`CFGR0.ADCPRE` 仅 2 位，
> 没有 /16），PCLK2 = 144 MHz 时最低只能到 18 MHz，超出手册 fADC ≤ 14 MHz 上限 28%。
> 降到 72 MHz 后 ADC = 9 MHz 完全合规；本项目 APB2 上只挂了 GPIO/ADC（USART1 未使用），
> 降频没有任何功能代价。

### 2.3 USB、调试与模式选择引脚

| 功能 | 引脚 | 说明 |
|---|---|---|
| USBHS D− | PB6 | USBHS 固定引脚，片内 PHY |
| USBHS D+ | PB7 | |
| SWDIO | PA13 | 调试口，保留可用（只关 JTAG、不动 SWD） |
| SWCLK | PA14 | 调试口，保留可用 |
| JTAG（JTDO/NJTRST/JTDI） | PB3 / PB4 / PA15 | 通过 `AFIO_PCFR1.SWJ_CFG = 010` **释放为普通 GPIO**，SWD 不受影响。其中 PB3 复用为 `BTN8` |
| 模式选择 | PB12 | 上电瞬间采样一次：**低电平 = CDC 配置模式**，高电平 = DS4 HID 模式。与 Share 键共用同一引脚 |
| 调试串口 TX | PA2 | `USART2_TX`，115200 8N1，printf 重定向（`Debug/debug.h` 中 `DEBUG = DEBUG_UART2`）。v0.2.0 主板未引出该引脚，需要看日志时自行飞线 |

### 2.4 摇杆（ADC1）

| 轴 | 引脚 | ADC 通道 | 方向处理 |
|---|---|---|---|
| 左摇杆 X（LX） | PC4 | `ADC_Channel_14` | **固件取反**（`v = 4095 - v`） |
| 左摇杆 Y（LY） | PC5 | `ADC_Channel_15` | 直通 |
| 右摇杆 X（RX） | **PA5** | `ADC_Channel_5` | **固件取反**（`v = 4095 - v`） |
| 右摇杆 Y（RY） | PA6 | `ADC_Channel_6` | 直通 |

PA5/PA6、PC4/PC5 配置为 `GPIO_Mode_AIN`。四路转换串行轮询，带**有界超时保护**
（约 400 µs 上限，超时返回该通道上一次有效值），ADC 异常不会把主循环拖死。

> 注意：两个摇杆的 X/Y 网络名在原理图上是交叉的（左摇杆 X 接 `JS2.Y_OUT`），
> 上表是实测确认后的**逻辑轴**映射，不要按网表引脚名直译。

### 2.5 按键

**除 L1、R1 外，所有按键均为内部上拉 + 低电平有效；L1 与 R1 为内部下拉 + 高电平有效**
（这两个网络板上没有外部上/下拉电阻）。

| DS4 功能 | 网络名 | 引脚 | GPIO 模式 | 有效电平 |
|---|---|---|---|---|
| 十字上 | UP | PE9 | 内部上拉 | 低 |
| 十字下 | DOWN | PE11 | 内部上拉 | 低 |
| 十字左 | LEFT | PE10 | 内部上拉 | 低 |
| 十字右 | RIGHT | PE8 | 内部上拉 | 低 |
| △ 三角（North） | Y | PE15 | 内部上拉 | 低 |
| ○ 圆圈（East） | B | PE14 | 内部上拉 | 低 |
| × 叉（South） | A | PE13 | 内部上拉 | 低 |
| □ 方块（West） | X | PB11 | 内部上拉 | 低 |
| **L1** | L1 | **PE7** | **内部下拉** | **高** |
| **R1** | R1 | **PB10** | **内部下拉** | **高** |
| L2（数字） | L2 | PB0 | 内部上拉 | 低 |
| R2（数字） | R2 | PB1 | 内部上拉 | 低 |
| L3（左摇杆按下） | JS2 S3/S4 | **PA7** | 内部上拉 | 低 |
| R3（右摇杆按下） | JS1 S3/S4 | PE1 | 内部上拉 | 低 |
| PS / Home | PS | PE12 | 内部上拉 | 低 |
| Share / Select | SW2 | PB12 | 内部上拉 | 低（同时是启动模式选择脚） |
| Options / Start | SW4 | PB14 | 内部上拉 | 低 |
| Touchpad 点击 | SW3 | PB13 | 内部上拉 | 低 |

> L1/R1 是数字按键，不是模拟扳机。DS4 报告里 L2/R2 只有 `0xFF`（按下）和 `0x00`（松开）
> 两种取值——本硬件没有扳机行程。

按键在固件里做 **4 帧一致性消抖**（零阻塞；8 kHz 下等效稳定窗 0.5 ms，1 kHz 下 4 ms，
250 Hz 下 16 ms）。

### 2.6 扩展按键（仅扫描，**未**映射到 DS4 报告）

| 位 | 引脚 | GPIO 模式 | 有效电平 | 状态 |
|---|---|---|---|---|
| BTN1 … BTN7 | PD1 … PD7 | 内部上拉 | 低 | 进入按键位图，**不上报** |
| BTN8 | PB3 | 内部上拉 | 低 | 同上；PB3 原为 JTDO，已关 JTAG 释放 |

`input_read_buttons()` 会产出 `INPUT_MASK_BTN1..8`，但 `gamepad.c` 不消费这些位，
因此永远不会进入 HID 报告。映射关系留给后续硬件版本确定。

### 2.7 明确**未**使用的引脚

| 引脚 | 说明 |
|---|---|
| PA0 / PA2（原 LED/KEY） | v0.1.0 模板遗留，已移除。PA0 空置；PA2 现为调试串口 TX |
| PB4、PB5、PA15 | PB4/PA15 已从 JTAG 释放、PB5 本就未用；三者均未接到任何器件 |
| PC0 ~ PC3、PE0、PE2 ~ PE6、PC10、PC12 | v0.1.0 的旧按键/摇杆引脚，v0.2.0 未连接 |

---

## 3. 编译与烧录

### 3.1 工具链要求

| 项目 | 要求 |
|---|---|
| IDE（推荐） | **MounRiver Studio 2.5.0 / 3.x** —— 仓库已带 `.cproject`、`.project`、`.wvproj` 和生成好的 `obj/makefile` |
| 编译器 | `riscv-none-embed-gcc`（xPack GNU RISC-V Embedded GCC 8.2.0，MRS 自带） |
| 架构参数 | `-march=rv32imacxw -mabi=ilp32 -Os` |
| USB 协议栈 | TinyUSB 0.21（git 子模块 `TinyUSB/`），克隆后先执行 `git submodule update --init --recursive` |

### 3.2 编译

工程是 MounRiver 预生成的，`obj/` 下已带 makefile：

```bash
cd obj
make -B all          # 全量重编
```

正常的体积输出（固件 0.1.0 / 硬件 v0.2.0）：

```text
   text    data     bss     dec     hex  filename
  27372     264   11792   39428    9a04  8khz-ds4-gamepad-ch32v307.elf
```

### 3.3 产物说明

| 文件 | 内容 |
|---|---|
| `obj/8khz-ds4-gamepad-ch32v307.elf` | 带调试符号的 ELF，用于 SWD 下载/在线调试 |
| `obj/8khz-ds4-gamepad-ch32v307.hex` | Intel HEX，用于 WCH-LinkUtility / MounRiver 烧录 |
| `obj/8khz-ds4-gamepad-ch32v307.bin` | 纯二进制（27636 字节），烧到 `0x08000000` |
| `obj/8khz-ds4-gamepad-ch32v307.map` | 链接映射表（符号 → 地址） |

### 3.4 烧录方法

1. **SWD（推荐）**：把 **WCH-LinkE** 接到 `PA13(SWDIO)`、`PA14(SWCLK)`、`GND`、`3V3`，
   在 MounRiver Studio 里点 *Download*，或用 WCH-LinkUtility 烧 `.hex`。
2. **USB ISP**：把 **BOOT0 拨到高电平**后重新插线，用 **WCHISPStudio** 通过 USB 烧
   `.bin`/`.hex`；烧完把 BOOT0 拨回低电平再插线运行。

首次烧录后如果配置扇区是空的，会自动加载出厂默认值，设备立刻枚举成 DualShock 4。

---

## 4. 配置与校准

### 4.1 进入 CDC 配置模式

1. 拔掉手柄。
2. **按住 Share 键（PB12）不放**。
3. 插上 USB 线，插好后松开。
4. 设备枚举为 **`0331:0001` "DS4 Configurator"** —— 一个 USB CDC 虚拟串口
   （Windows 10+/Linux/macOS 都免驱）。
5. 打开[网页配置器](https://github.com/P1nkDog/ds4-gamepad-configurator)，点 **连接**，
   选中对应串口。

回到手柄模式：不按 Share 拔插一次，或在网页上发 **重启设备**（MSP_REBOOT）。

### 4.2 MSP v1 协议

帧格式（MSP v1，`$` 开头，异或校验）：

```text
请求：'$' 'M' '<' <size:1> <cmd:1> <payload:size> <crc:1>
应答：'$' 'M' '>' <size:1> <cmd:1> <payload:size> <crc:1>
      crc = size、cmd 与全部 payload 字节的逐字节异或
```

+ `size` 最大 254，整帧最大 260 字节。
+ **错误应答同样使用 `>` 方向字节**，错误码放在 `payload[0]`。上位机解析必须看状态码，
  不能只靠方向字节区分成功/失败（标准 MSP 的 `!` 在本实现中未使用）。
+ 半包保护：状态机非空闲时若 50 ms 内没有新字节，自动丢弃半包回到空闲态，
  因此断线/网页刷新不会让后续命令被"吃掉"。

| 命令码 | 名称 | 载荷 → 应答 |
|---|---|---|
| `0x01` | MSP_API_VERSION | — → `[major, minor]` = `[0, 1]` |
| `0x02` | MSP_FC_VARIANT | — → ASCII `"P1NKD-DS4"` |
| `0x03` | MSP_FC_VERSION | — → `[0, 1, 0]` |
| `0x04` | MSP_REBOOT | `['R']` → `[status]`，随后设备复位 |
| `0x10` | MSP_CONFIG_GET | `[id]` → `[id, version, data…]` |
| `0x11` | MSP_CONFIG_SET | `[id, data…]` → `[id, status]`（只写 RAM，未落盘） |
| `0x12` | MSP_CONFIG_SAVE | — → `[status]`（把所有脏组写入 Flash） |
| `0x13` | MSP_CONFIG_RESET | `[id]` → `[id, status]`（单组恢复默认，只改 RAM） |
| `0x14` | MSP_CONFIG_STATUS | — → `[count][id, size_lo, size_hi] × count` |
| `0x15` | MSP_JOYSTICK_RAW | — → `[4][原始 LX,LY,RX,RY][映射后 LX,LY,RX,RY]`，均为 u16 LE |
| `0x16` | MSP_CALIB_START | — → `[status]` |
| `0x17` | MSP_CALIB_SAMPLE | — → `[4][center LX,LY,RX,RY]`，u16 LE |
| `0x18` | MSP_CALIB_TRACK | — → `[status]` |
| `0x19` | MSP_CALIB_END | — → `[status][4][min,center,max ×4]` |
| `0x1A` | MSP_CALIB_CANCEL | — → `[status]` |

状态码：`0` 成功 · `1` 通用错误 · `2` 无效 ID · `3` 数据非法 · `4` Flash 写失败。

### 4.3 配置组

| ID | 配置组 | 大小 | 字段 | 合法性约束 |
|---|---|---|---|---|
| 1 | `joystick_calib` | 24 B | 4 组（`min`, `center`, `max`）u16 | 每轴 `min < center < max ≤ 4095` |
| 2 | `gamepad_settings` | 2 B | `deadzone`（百分比） | `0 ~ 50` |
| 3 | `polling_rate` | 1 B | `level` | `0 ~ 5`（250 / 500 / 1k / 2k / 4k / 8k Hz） |

非法值在**写入 RAM 之前**就被拒绝（返回 `MSP_STATUS_INVALID_DATA`）；保存时只要有任何
一组当前非法，整次保存直接拒绝——不会出现"合法 CRC 包裹非法数据"的情况。

### 4.4 摇杆校准流程

五步，全部由网页驱动：

1. `CALIB_START` —— 快照当前校准值（回滚点），状态 → `START`。
2. **摇杆回中**，然后 `CALIB_SAMPLE` —— 每轴平均 16 帧，状态 → `CENTER_SAMPLED`。
   该状态下轴输出强制为中点，避免"中心已更新、min/max 还是旧值"时预览出现半量程跳变。
3. `CALIB_TRACK` —— 状态 → `TRACKING`，固件只在 RAM 里累计各轴极值
   （活动配置在校准全程仍保持上一次的合法校准，不会把半成品写进 Flash）。
4. **把两个摇杆推到各个方向极限**，然后 `CALIB_END` —— 校验 `min < center < max`、
   每侧行程至少 200 个 ADC count，再过一遍配置层校验回调，全部通过才置脏并落盘；
   任何一项不过就回滚到快照并返回错误。
5. `CALIB_CANCEL` —— 任意时刻放弃校准并回滚，状态 → `IDLE`。

越阶调用（例如没采样中心就直接跟踪）会被明确拒绝并回 `ERROR`，网页能区分"真的执行了"
和"被状态机忽略"。

### 4.5 回报率——改完必须重启

回报率是写进配置描述符的 HID **`bInterval`** 字段的（`bInterval = 6 - 档位`：
250 Hz → 6，8 kHz → 1）。描述符只在枚举时被主机读取，所以：

> **改完回报率必须点网页上的「重启设备」（或拔插一次）才会生效。**

死区和校准则不同，改完立刻作用于下一帧报告。

关于真实 8 kHz 的几点提醒：

+ 主机必须支持 125 µs 的高速中断轮询（Linux `usbhid` 支持；Windows 取决于控制器驱动）。
+ 数据线与 HUB 必须是真高速（480 Mbps）。插在全速口上设备仍能用，但实际速率会退化到 1 ms。
+ 发送是令牌驱动的：上一帧传输完成才会组下一帧，所以实际速率**等于主机的轮询速率**，
  不会出现突发连发。

### 4.6 Flash 存储布局

| 区域 | 地址 | 大小 | 内容 |
|---|---|---|---|
| 固件 | `0x08000000` – `0x0803DFFF` | ≤ 248 KB | 代码 + 只读数据；链接脚本里有一条 `ASSERT`，固件一旦长到触碰配置扇区就直接链接失败 |
| 配置区 A（主） | `0x0803E000` | 4 KB | `ConfigHeader{magic 0xCF07, version 1, count}` + TLV 记录 + 尾标 + CRC16 |
| 配置区 B（暂存） | `0x0803F000` | 4 KB | 同上布局，每次保存先写这里 |

保存流程：在 RAM 里拼好镜像 → **与主扇区 A 逐字节比对，完全一致就跳过整次擦写**
（保护 Flash 寿命）→ 擦 B、写 B、校验 B → 擦 A、写 A、校验 A。
任一时刻两个扇区至少有一个是完整的 CRC 有效镜像，启动时优先读 A、失败回退 B。
记录长度不足或内容过不了校验时跳过该组，保留出厂默认值。

---

## 5. DS4 报告布局（参考）

输入报告 ID `0x01`，总长 64 字节（报告描述符声明 63 字节载荷 + 报告 ID）：

| 字节 | 内容 |
|---|---|
| 0 | 报告 ID `0x01` |
| 1–4 | 左 X、左 Y、右 X、右 Y（0–255，中位 128） |
| 5 | 低半字节 = 十字键（0=上，1=右上 … 7=左上，0x0F=松开）；高 4 位 = □ × ○ △ |
| 6 | L1、R1、L2、R2、Share、Options、L3、R3 |
| 7 | PS（bit0）、Touchpad（bit1）、6-bit 滚动帧计数器（bit2–7） |
| 8–9 | 左/右扳机（本硬件只有 0x00 或 0xFF） |
| 10–63 | 厂商自定义区：IMU / 触摸板 / 电量等。字节 30 固定填 `0x1B`（满电 + USB 供电） |

认证握手用的 Feature 报告：`0x02` 校准（36 B）、`0x03` 定义（47 B）、`0x12` MAC（15 B）、
`0xA3` 版本/日期（48 B）、`0xF2` 签名状态（15 B）、`0xF3` 复位认证（7 B）。
`0xF1`（签名 nonce）故意 STALL —— 本固件不做真认证。

---

## 6. 目录结构

```text
App/          主循环、TinyUSB 配置（tusb_config.h）
Bsp/          板级初始化、中断向量（USBHS/TIM2）、时钟树、Link.ld
Config/       配置注册表、TLV + CRC16 存储、Flash HAL
Driver/
  ds4/        DS4 报文组装、报告描述符、Feature 报告
  gamepad/    按键/十字键映射到 GamepadState
  input/      GPIO 按键扫描（含消抖）+ ADC 摇杆采样
  joystick/   校准状态机、死区与满量程映射
  msp/        MSP v1 状态机 + 命令分发
  cdc/        CDC 管道，把字节喂给 MSP 解析器
  usb/        描述符路由（HID / CDC）、USB 设备栈初始化
  timer/      TIM2 微秒时基（micros()）
  stat/       可选的代码段耗时统计（默认编译为空）
Core/         RISC-V 内核支持（PFIC/NVIC 封装、CSR 读写）
Debug/        延时函数与 printf 重定向（USART2）
Startup/      startup_ch32v30x_D8C.S（向量表、复位入口）
Peripheral/   WCH CH32V30x 标准外设库
TinyUSB/      TinyUSB 0.21（git 子模块）
obj/          编译产物（已被 git 忽略）
Docs/         历史设计文档归档（可能过时）
PCB/          硬件版本（v0.1.0、v0.2.0）
```

---

## 7. 开源致谢与许可

本固件站在这些开源项目的肩膀上：

+ **[TinyUSB](https://github.com/hathach/tinyusb)** —— 作者 Ha Thach（hathach）及贡献者。
  USB 设备栈、WCH CH32V307 高速口移植层、HID/CDC 类驱动全部来自 TinyUSB 0.21（MIT）。
+ **WCH / 南京沁恒微电子** —— CH32V30x 标准外设库、启动文件，以及 USBHS 时钟/PLL 上电序列，
  取自 CH32V307 EVT 官方例程。
+ **[GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE)** —— DS4 报告描述符、
  Feature 报告握手数据、"报告 ID 内嵌在载荷里发送"这一技巧的参考实现。
+ **DS4/PS4 协议研究社区** —— 输入报告布局与认证握手行为来自各家逆向分析文档。
+ **[betaflight](https://github.com/betaflight/betaflight)** —— 参数分组（TLV + 校验回调）
  配置设计的灵感来源。

本项目以 **MIT 许可证**发布，详见 [LICENSE](LICENSE)。
`Peripheral/`、`Core/`、`Startup/`、`Debug/` 源自 WCH EVT 包，保留其原始版权声明。
