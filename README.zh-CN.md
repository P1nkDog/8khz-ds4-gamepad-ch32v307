# CH32V307 DS4 8K 手柄

> 8000Hz 回报率的 PS4 DualShock 4 手柄完整方案：固件 + PCB + 网页配置器

`v0.1.0` · 配套网页配置器：[P1nkDog/ds4-gamepad-configurator](https://github.com/P1nkDog/ds4-gamepad-configurator)

[English](./README.md) | 中文

---

## 功能特性

- **8kHz 回报率**：走 CH32V307 的 USB 高速口（480 Mbps），HID 端点 `bInterval = 1` 即 125 µs。可在 250 / 500 / 1k / 2k / 4k / 8k 六档间配置，默认 1k
- **原生 DS4 设备**：按 DualShock 4 实现报告描述符与 64 字节输入报告，主机直接识别为 PS4 手柄，不需要驱动或按键映射软件
- **HID / CDC 双模式**：上电瞬间读 Share 键（PB12）决定这次启动做手柄还是做串口配置设备
- **MSP v1 配置协议**：15 条命令，覆盖版本查询、配置读写、摇杆实时值、分步校准、设备重启
- **配置存 Flash**：双扇区交替写入（`0x08046000` / `0x08047000`）+ magic `0xCF07` + CRC16 校验，断电写坏可回退另一块
- **摇杆处理**：四轴各自校准（min / center / max）、ADC 取反、死区、死区外满量程映射
- **网页可配**：死区、四轴校准、回报率都能在浏览器里改，走 Web Serial，无需装驱动

> 8kHz 需主机与线材都支持 USB 高速；实际速率受主机 USB 调度影响，插到全速口上会自然降级。

## 硬件

### 主控与关键器件

| 项 | 说明 |
|---|---|
| 主控 | CH32V307VCT6（RISC-V RV32IMACXW，288K Flash / 32K RAM，内置 USBHS PHY） |
| USB | USB 2.0 高速（480 Mbps），设备描述符 `bcdUSB = 0x0200` |
| 摇杆 | 双模拟摇杆，ADC1 采样 |
| 按键 | 轻触开关 + 1 个 BOOT0 拨动开关 |
| 板载 LED / 独立按键 | 代码预留 PA0（LED，低电平点亮）/ PA2（按键），**当前板未引出**，见 `Bsp/board.h` |

### 引脚定义

**摇杆（ADC1）**

| 轴 | 引脚 | ADC 通道 |
|---|---|---|
| 左摇杆 X | PC4 | ADC14 |
| 左摇杆 Y | PC5 | ADC15 |
| 右摇杆 X | PA6 | ADC6 |
| 右摇杆 Y | PA7 | ADC7 |

**按键（低电平有效，内部上拉）**

| 功能 | 引脚 | 功能 | 引脚 |
|---|---|---|---|
| 十字上 | PE9 | L1 | PE7 |
| 十字下 | PE11 | R1 | PB10 |
| 十字左 | PE10 | L2 | PB0 |
| 十字右 | PE8 | R2 | PB1 |
| △ / Y | PE15 | 左摇杆按下（L3） | PA5 |
| ○ / B | PE14 | 右摇杆按下（R3） | PE1 |
| × / A | PE13 | PS | PE12 |
| □ / X | PB11 | Touchpad（SW3） | PB13 |
| Options / Start（SW4） | PB14 | Share / Select（SW2） | PB12 |

**拓展按键（已扫描，尚未上报给 DS4）**

| 编号 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| 引脚 | PD3 | PD4 | PD5 | PD6 | PD7 | PB3 | PB4 | PE0 |

**开关**

| 器件 | 功能 | 说明 |
|---|---|---|
| SW5 | BOOT0 | 拨到 **1** → 3.3V，上电进芯片 Bootloader（烧录用）；拨回 **0** → GND，正常运行固件 |
| SW2 | Share / Select | **按住上电**（把 PB12 拉低）→ 以 CDC 配置模式启动；松着上电 → HID 手柄模式 |

### PCB

硬件工程按版本分目录存放，当前版本为 [`PCB/v0.1.0/`](./PCB/v0.1.0/)，共三块板：**主板**、**Type-C 小板**、**拼板**。

| 文件 | 内容 |
|---|---|
| `8khz-ds4-gamepad-ch32v307.epro2` | EDA 工程源文件，可直接导入继续改图 |
| `8khz-ds4-gamepad-ch32v307.zip` | 原理图 PDF ×2：`SCH_Schematic1_1-主板`、`SCH_Schematic2_1-小板` |
| `main board.zip` | 主板 Gerber（打样直接上传这个包） |
| `type-c board.zip` | Type-C 小板 Gerber |
| `panel.zip` | 拼板（panel）Gerber——多块板拼在一起打样时用 |

三个 Gerber 包内容一致，各 16 个文件：铜层（GTL/GBL）、丝印（GTO/GBO）、阻焊（GTS/GBS）、钢网（GTP/GBP）、板框（GKO）、钻孔图（GDL/GDD）、PTH/NPTH/过孔钻孔（DRL）、飞针测试文件，并附 `PCB下单必读.txt`——**直接整包上传打板厂即可下单**。

> 目前没有 BOM 和贴片坐标文件，要整板 SMT 贴片需要另外导出。

## 快速开始

### 前置条件

- **MounRiver Studio（MRS）2.5.0** 或更新版本，自带 `riscv-none-embed-gcc`
- 首次 clone 后拉取子模块（TinyUSB 是 submodule，不拉会导致编译失败）：

  ```bash
  git submodule update --init --recursive
  ```

- 烧录二选一：**WCH-Link**（MRS 里直接 Download）或**任意 USB 线 + 网页烧录**

### 编译

1. MRS → `File` → `Import` → `Existing MounRiver Studio Project`，选中本目录
2. `Project` → `Build Project`（Ctrl+B）
3. 产物在 `obj/`：`8khz-ds4-gamepad-ch32v307.hex` / `.elf` / `.map`

   > 文件名 = MRS 工程名，取自 `.project` 的 `<name>` 字段（`.cproject` 用 `artifactName="${ProjName}"`），**与克隆目录叫什么无关**。本仓库的工程名已设为 `8khz-ds4-gamepad-ch32v307`；若你在 MRS 里重命名工程，产物名会跟着变。

工程根目录没有 `Makefile`：`obj/`（含 `makefile`、`.o`、`.hex`）是 MRS 首次 Build 时自动生成的，且已在 `.gitignore` 里——**刚克隆下来没有 `obj/` 属正常**。命令行手动编译需自行指定工具链路径，一般直接用 MRS 即可。

### 烧录

**方式一：网页烧录（推荐，免装工具，Windows）**

1. 主板底下 **BOOT0 拨到 1**
2. USB 线连接电脑
3. 用 [Zadig](https://zadig.akeo.ie/) 给 `USB ID 4348 55E0` 的设备装 **WinUSB** 驱动（只需一次）
4. 打开[网页配置器](https://github.com/P1nkDog/ds4-gamepad-configurator)的「固件烧录」页 → 右上角 **[连接]** → 选「来自 WinChipHead 的未知设备」
5. 选择 `obj/8khz-ds4-gamepad-ch32v307.hex`，或用「在线烧录」里列出的版本 → **[烧录]**
6. **BOOT0 拨回 0，重新上电**

**方式二：MRS + WCH-Link**

接好 WCH-Link → MRS 点 `Download`（BOOT0 保持 0 即可，WCH-Link 走调试口）。

**方式三：WCHISPTool**

Windows 下的官方烧录工具，选 USB 方式 → 选 `obj/8khz-ds4-gamepad-ch32v307.hex` → 下载。同样需要先把 BOOT0 拨到 1。

## 项目结构

```
8khz-ds4-gamepad-ch32v307/
├── App/
│   ├── main.c                  # 主循环：tud_task + HID/CDC 分支
│   └── tusb_config.h           # TinyUSB 配置（HID + CDC、端点缓冲）
├── Bsp/
│   ├── board_init.c/.h         # 时钟、USBHS、LED、按键、SysTick
│   ├── Link.ld                 # 链接脚本：288K Flash + 32K RAM
│   ├── ch32v30x_it.c           # 中断服务
│   ├── system_ch32v30x.c       # 系统时钟
│   └── ch32v30x_conf.h         # 外设库配置
├── Config/
│   ├── config.c/.h             # 配置组注册、TLV 序列化、双扇区切换
│   ├── config_crc.h            # CRC16
│   └── config_hal.c/.h         # Flash 扇区读写（0x08046000 / 0x08047000）
├── Core/                       # RISC-V 内核支持（CSR / PFIC / SysTick）
├── Driver/
│   ├── ds4/                    # DS4 报告描述符 + 64 字节输入报告
│   ├── cdc/                    # CDC 虚拟串口（配置模式）
│   ├── usb/                    # 描述符路由 + HID/CDC 模式选择
│   ├── msp/                    # MSP v1 帧编解码与命令处理
│   ├── input/                  # ADC 摇杆采样 + 按键扫描
│   ├── joystick/               # 取反、死区、满量程映射、校准状态机
│   ├── gamepad/                # 手柄状态组装（按键 → DS4 字段）
│   └── timer/                  # TIM2 微秒定时器
├── Peripheral/                 # CH32V30x 标准外设库（28 个模块）
├── Startup/                    # 启动文件（使用 D8C）
├── TinyUSB/                    # TinyUSB 子模块 v0.20.0
├── PCB/v0.1.0/                 # 硬件工程（EDA 源文件 / 原理图 PDF / Gerber）
├── obj/                        # 编译产物（gitignore）
├── README.md                   # 英文说明
├── README.zh-CN.md             # 中文说明（本文件）
└── LICENSE                     # GPL-3.0
```

## 固件架构

```
App/main.c ── 主循环（tud_task() 常驻轮询）
│
├─ HID 模式（松着上电）
│    gamepad_process()
│      ├─ input_read_buttons()   按键扫描（GPIO）
│      ├─ joystick_process()     ADC → 取反 → 死区 → 满量程映射
│      └─ ds4_process()          组装 64 字节报告
│    → TinyUSB HID 中断端点（bInterval=1 → 125 µs）
│
└─ CDC 模式（按住 Share 上电）
     cdc_process()
       └─ msp_handler()          MSP v1 命令解析
            ├─ CONFIG_GET/SET/SAVE/RESET  → Config
            ├─ CALIB_START/SAMPLE/TRACK/END → joystick
            ├─ JOYSTICK_RAW            → joystick 原始值
            └─ REBOOT                  → 软复位
     → TinyUSB CDC → 浏览器 Web Serial

Config ── 三个配置组 + TLV 序列化 + CRC16 + 双扇区交替（0x08046000 / 0x08047000）
```

**上电顺序**：`board_init()` → `config_init()`（读 Flash、校验，失败回退默认值）→ `us_timer_init()` → `usb_desc_init()`（读 PB12 决定模式）→ `tud_init()` → `gamepad_init()`。

**定速上报**：主循环用 TIM2 微秒计时，`micros() - last >= 配置间隔` 时才跑一帧 `gamepad_process()` + `ds4_process()`。间隔由配置组 `polling_rate` 查表得出（8k → 125 µs）。

**双模式互斥**：CDC 模式下不跑 `gamepad_process()`，手柄数据完全不上报——这是 USB 设备每次只枚举一种配置的结果，不是缺陷。

## 网页配置器

[P1nkDog/ds4-gamepad-configurator](https://github.com/P1nkDog/ds4-gamepad-configurator) —— 纯前端页面，两条独立通道：

| 用途 | 通道 | 设备状态 |
|---|---|---|
| 改参数、校准摇杆 | Web Serial（CDC） | 固件正常运行，CDC 模式 |
| 刷固件 | WebUSB（WCH ISP，`4348:55E0`） | 固件未运行，芯片在 Bootloader |

配置流程：按住 **Share** 插线（或按 **RST**）进 CDC 模式 → 浏览器打开页面 → 点 **[连接]** → 改参数 → 底边栏 **[保存]**（改回报率还要 **[重启设备]**）。

## 配置说明

### 配置组

| ID | 名称 | 结构 | 版本 | 默认值 | 说明 |
|---|---|---|---|---|---|
| 1 | `joystick_calib` | `JoystickCalibConfig` | 1 | 四轴 `min=0 / center=2048 / max=4095` | 每轴独立校准 |
| 2 | `gamepad_settings` | `GamepadSettings` | 1 | `deadzone = 1`（%） | 死区为**全手柄共用**一个值 |
| 3 | `polling_rate` | `PollingRateConfig` | 1 | `POLL_RATE_1000` | 六档：250 / 500 / 1k / 2k / 4k / 8k |

存储格式是 TLV：文件头（magic `0xCF07` + version + count）→ 若干记录（type / length / version / flags / data）→ 结束符。读取时版本不匹配的记录会被忽略并回退默认值，所以改结构体要同步升版本号。

### 摇杆处理

1. **ADC 采样** → 原始 12 位值
2. **硬件取反**（部分电位器方向相反，统一在这里翻正）
3. **死区**：以 `center` 为基准，死区以内直接输出中点 `2048`
4. **死区外满量程映射**：把 `[center ± dz, min/max]` 线性拉伸到 `[0, 2048]` / `[2048, 4095]`，推到极限就是满值
5. **校准四步**（对应 `CALIB_START / SAMPLE / TRACK / END`）：采中心（16 次平均）→ 跟踪各轴极值 → 写回配置并保存

### TinyUSB 配置

`App/tusb_config.h` 里几个关键项：

| 宏 | 值 | 作用 |
|---|---|---|
| `CFG_TUD_HID` / `CFG_TUD_CDC` | 1 / 1 | 同时编译两个设备类，运行时按模式选配置 |
| `CFG_TUD_ENDPOINT0_SIZE` | 64 | 控制端点 |
| `CFG_TUD_HID_EP_BUFSIZE` | 64 | 与 DS4 报告长度一致 |
| `CFG_TUD_CDC_*_BUFSIZE` | 高速 512 / 全速 64 | 串口收发缓冲 |
| `CFG_TUSB_OS` | `OPT_OS_NONE` | 裸机（无 RTOS），`tud_task()` 在主循环里轮询 |

### 按键映射

物理按键 → DS4 字段在 `Driver/gamepad/gamepad.c` 里硬编码：

| 物理键 | DS4 字段 | 物理键 | DS4 字段 |
|---|---|---|---|
| □ / X（PB11） | `buttonWest` | L1（PE7） | `buttonL1` |
| × / A（PE13） | `buttonSouth` | R1（PB10） | `buttonR1` |
| ○ / B（PE14） | `buttonEast` | L2（PB0） | `buttonL2` |
| △ / Y（PE15） | `buttonNorth` | R2（PB1） | `buttonR2` |
| Share（PB12） | `buttonSelect` | L3（PA5） | `buttonL3` |
| Options（PB14） | `buttonStart` | R3（PE1） | `buttonR3` |
| PS（PE12） | `buttonHome` | 十字键 | `dpad`（支持八向） |
| Touchpad（PB13） | `buttonTouchpad` | | |

## 项目状态

**已实现**

- DS4 HID 上报（64 字节：摇杆、按键、十字键、PS / Touchpad）
- 8kHz 定速上报（USB 高速口）+ 六档可配
- CDC 配置模式 + MSP v1（15 条命令）
- 配置双扇区存储、CRC 校验、断电回退
- 四轴校准与死区映射
- 网页端改参数、校准、烧录

**未实现 / 已知限制**

- 拓展按键 BTN1~BTN8 只扫描、未上报（DS4 输入报告里没有对应字段）
- 扳机是**数字**开关，没有模拟行程，因此没有扳机曲线
- 按键映射硬编码，不支持宏、连发、重映射
- 死区是全局单值，左右摇杆不能分别设
- 配置模式与手柄模式互斥，切换要重新插线
- 无陀螺仪 / 加速度计；Touchpad 只支持按下，无坐标

## 参考资料

| 来源 | 用途 |
|---|---|
| [TinyUSB](https://github.com/hathach/tinyusb) | USB 协议栈（含 WCH CH32 USBHS 驱动） |
| [wchisp-web](https://github.com/P1nkDog/wchisp-web) | 网页端 WCH ISP 烧录实现（属配置器仓库，非本固件） |
| [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) | 手柄功能与按键布局参考 |
| [Betaflight](https://github.com/betaflight/betaflight) | 配置组 / MSP 协议设计参考 |
| DualShock 4 报告描述符 | 以实机抓取的描述符为基准 |

## 许可证

[GPL-3.0-only](./LICENSE)。可以自由使用、修改、分发，但衍生工程必须同样以 GPL-3.0 开源，且不提供担保。

## 致谢

- [TinyUSB](https://github.com/hathach/tinyusb) —— USB 协议栈，设备类、端点管理与 WCH 移植层都来自它
- 沁恒微电子（WCH）—— CH32V307 芯片、标准外设库与官方例程
- [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE)、[Betaflight](https://github.com/betaflight/betaflight) —— 按键布局与配置系统的思路来源
