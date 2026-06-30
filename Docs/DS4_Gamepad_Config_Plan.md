# DS4 手柄功能实现 + 配置文件系统方案

---

## 0. 项目发展路线图

### 当前状态

项目目前处于 **模板阶段**：基础工程已搭建（CH32V307 + TinyUSB），系统时钟 144MHz，USART 调试输出正常，但 `main.c` 仍是 WCH 模板代码，无任何 USB 或手柄功能。编译输出约 110KB ELF，TinyUSB 编译了全部 70+ 源文件（实际只需 7 个）。

### 总体路线

```
阶段 0 ──── 工程瘦身与基础准备
  │
  ▼
阶段 1 ──── DS4 手柄核心功能移植
  │
  ▼
阶段 2 ──── 配置文件系统
  │
  ▼
阶段 3 ──── 输入驱动与按键映射
  │
  ▼
阶段 4 ──── PC 端配置工具
  │
  ▼
阶段 5 ──── 扩展功能
  │
  ▼
阶段 6 ──── 稳定性与量产
```

---

### 阶段 0：工程瘦身与基础准备

**目标**：清理不需要的编译产物，为后续开发腾出 Flash 空间，建立正确的工程结构。

#### 0.1 当前问题分析

**TinyUSB 编译膨胀**：当前项目编译了 TinyUSB 的全部源文件，包括所有 USB 设备类、主机栈和 30+ 个平台移植驱动。

| 对比项 | 当前项目 | 参考项目 | 差异 |
|--------|----------|----------|------|
| TinyUSB .o 文件数 | **74 个** | **7 个** | 多编译了 67 个无用文件 |
| TinyUSB .o 目录大小 | **2.2MB** | **668KB** | 多出 1.5MB 目标文件 |
| ELF 文件大小 | **108KB** | **308KB** | 当前项目反而更小（因为没有手柄功能） |
| USB 速度配置 | `OPT_MODE_DEFAULT_SPEED` | `OPT_MODE_HIGH_SPEED` | 效果相同，见下方说明 |
| `bsp/` 目录 | **缺失** | 完整 | 需要创建 |
| `CFG_TUSB_MCU` 预定义 | **已定义** | 通过编译器定义 | 已在 tusb_config.h 中设置 |

**当前项目编译的 74 个 TinyUSB 文件清单**：

不需要的设备类 (20 个 .o)：
```
class/audio/audio_device.o        ← 不需要
class/bth/bth_device.o            ← 不需要
class/cdc/cdc_device.o            ← 不需要
class/cdc/cdc_host.o              ← 不需要
class/cdc/cdc_rndis_host.o        ← 不需要
class/dfu/dfu_device.o            ← 不需要
class/dfu/dfu_rt_device.o         ← 不需要
class/hid/hid_host.o              ← 不需要 (只需要 hid_device)
class/midi/midi_device.o          ← 不需要
class/midi/midi_host.o            ← 不需要
class/msc/msc_device.o            ← 不需要
class/msc/msc_host.o              ← 不需要
class/mtp/mtp_device.o            ← 不需要
class/net/ecm_rndis_device.o      ← 不需要
class/net/ncm_device.o            ← 不需要
class/usbtmc/usbtmc_device.o      ← 不需要
class/vendor/vendor_device.o      ← 不需要
class/vendor/vendor_host.o        ← 不需要
class/video/video_device.o        ← 不需要
```

不需要的主机栈 (2 个 .o)：
```
host/hub.o                        ← 不需要 (只用设备模式)
host/usbh.o                       ← 不需要
```

不需要的平台移植驱动 (41 个 .o)：
```
portable/analog/max3421/hcd_max3421.o       ← 不需要
portable/bridgetek/ft9xx/dcd_ft9xx.o        ← 不需要
portable/chipidea/ci_fs/dcd_ci_fs.o         ← 不需要
portable/chipidea/ci_hs/dcd_ci_hs.o         ← 不需要
portable/chipidea/ci_hs/hcd_ci_hs.o         ← 不需要
portable/dialog/da146xx/dcd_da146xx.o       ← 不需要
portable/ehci/ehci.o                        ← 不需要
portable/mentor/musb/dcd_musb.o             ← 不需要
portable/mentor/musb/hcd_musb.o             ← 不需要
portable/microchip/pic/dcd_pic.o            ← 不需要
portable/microchip/pic32mz/dcd_pic32mz.o    ← 不需要
portable/microchip/samd/dcd_samd.o          ← 不需要
portable/microchip/samd/hcd_samd.o          ← 不需要
portable/microchip/samg/dcd_samg.o          ← 不需要
portable/microchip/samx7x/dcd_samx7x.o      ← 不需要
portable/mindmotion/mm32/dcd_mm32f327x_otg.o ← 不需要
portable/nordic/nrf5x/dcd_nrf5x.o           ← 不需要
portable/nuvoton/nuc120/dcd_nuc120.o        ← 不需要
portable/nuvoton/nuc121/dcd_nuc121.o        ← 不需要
portable/nuvoton/nuc505/dcd_nuc505.o        ← 不需要
portable/nxp/khci/dcd_khci.o                ← 不需要
portable/nxp/khci/hcd_khci.o                ← 不需要
portable/nxp/lpc17_40/dcd_lpc17_40.o        ← 不需要
portable/nxp/lpc_ip3511/dcd_lpc_ip3511.o    ← 不需要
portable/ohci/ohci.o                        ← 不需要
portable/raspberrypi/pio_usb/dcd_pio_usb.o  ← 不需要
portable/raspberrypi/pio_usb/hcd_pio_usb.o  ← 不需要
portable/raspberrypi/rp2040/dcd_rp2040.o    ← 不需要
portable/raspberrypi/rp2040/hcd_rp2040.o    ← 不需要
portable/raspberrypi/rp2040/rp2040_usb.o    ← 不需要
portable/renesas/rusb2/dcd_rusb2.o          ← 不需要
portable/renesas/rusb2/hcd_rusb2.o          ← 不需要
portable/renesas/rusb2/rusb2_common.o       ← 不需要
portable/sony/cxd56/dcd_cxd56.o             ← 不需要
portable/st/stm32_fsdev/dcd_stm32_fsdev.o   ← 不需要
portable/st/typec/typec_stm32.o             ← 不需要
portable/sunxi/dcd_sunxi_musb.o             ← 不需要
portable/synopsys/dwc2/dcd_dwc2.o           ← 不需要
portable/synopsys/dwc2/dwc2_common.o        ← 不需要
portable/synopsys/dwc2/hcd_dwc2.o           ← 不需要
portable/template/dcd_template.o            ← 不需要
portable/template/hcd_template.o            ← 不需要
portable/ti/msp430x5xx/dcd_msp430x5xx.o     ← 不需要
portable/valentyusb/eptri/dcd_eptri.o       ← 不需要
```

不需要的其他模块 (4 个 .o)：
```
typec/usbc.o                      ← 不需要
portable/wch/hcd_ch32_usbfs.o     ← 不需要 (只用设备模式，不用主机模式)
```

**只需要保留的 7 个文件**：
```
src/tusb.o                                    ← TinyUSB 核心
src/common/tusb_fifo.o                        ← FIFO 队列
src/device/usbd.o                             ← USB 设备核心
src/device/usbd_control.o                     ← USB 控制传输
src/class/hid/hid_device.o                    ← HID 设备类
src/portable/wch/dcd_ch32_usbfs.o             ← WCH USB FS 驱动
src/portable/wch/dcd_ch32_usbhs.o             ← WCH USB HS 驱动
```

#### 0.2 .cproject 修改方法

MounRiver Studio 的 `.cproject` 使用 `excluding` 属性排除不需要的源文件目录。当前排除列表在第 270 行：

```
<entry excluding="Startup/startup_ch32v30x_D8.S|TinyUSB/test|TinyUSB/examples|TinyUSB/docs|TinyUSB/lib|TinyUSB/hw|TinyUSB/tools|..."
```

需要在 `excluding` 属性中追加以下排除项（用 `|` 分隔）：

```
需要排除的 TinyUSB 子目录:
TinyUSB/src/class/audio
TinyUSB/src/class/bth
TinyUSB/src/class/cdc
TinyUSB/src/class/dfu
TinyUSB/src/class/hid/hid_host.c        ← 注意：只排除 host，保留 hid_device.c
TinyUSB/src/class/midi
TinyUSB/src/class/msc
TinyUSB/src/class/mtp
TinyUSB/src/class/net
TinyUSB/src/class/usbtmc
TinyUSB/src/class/vendor
TinyUSB/src/class/video
TinyUSB/src/host
TinyUSB/src/portable/analog
TinyUSB/src/portable/bridgetek
TinyUSB/src/portable/chipidea
TinyUSB/src/portable/dialog
TinyUSB/src/portable/ehci
TinyUSB/src/portable/mentor
TinyUSB/src/portable/microchip
TinyUSB/src/portable/mindmotion
TinyUSB/src/portable/nordic
TinyUSB/src/portable/nuvoton
TinyUSB/src/portable/nxp
TinyUSB/src/portable/ohci
TinyUSB/src/portable/raspberrypi
TinyUSB/src/portable/renesas
TinyUSB/src/portable/sony
TinyUSB/src/portable/st
TinyUSB/src/portable/sunxi
TinyUSB/src/portable/synopsys
TinyUSB/src/portable/template
TinyUSB/src/portable/ti
TinyUSB/src/portable/valentyusb
TinyUSB/src/typec
```

> **注意**：`.cproject` 是 XML 文件，可以用文本编辑器直接修改。MounRiver Studio 重新打开项目后会自动加载。
> 如果不想手动编辑 `.cproject`，也可以在 MounRiver Studio 中右键项目 → Properties → C/C++ Build → Settings → Source，手动添加排除路径。

#### 0.3 tusb_config.h 确认

当前 `tusb_config.h` 配置已正确，**无需修改**：

```c
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED  // ✅ 不需要改
#define CFG_TUSB_MCU OPT_MCU_CH32V307                 // ✅ 已定义
```

**为什么 `OPT_MODE_DEFAULT_SPEED` 就是 High-Speed？**

追踪 TinyUSB 源码逻辑链：
1. `tusb_mcu.h` 中 CH32V307 定义：`TUP_RHPORT_HIGHSPEED = CFG_TUD_WCH_USBIP_USBHS`
2. `CFG_TUD_WCH_USBIP_USBHS` 未定义时默认为 1（因为 USBFS 也未定义，默认 0，取反得 1）
3. `tusb_option.h`：`TUD_OPT_HIGH_SPEED = (0x0000 ? ... : TUP_RHPORT_HIGHSPEED) = 1`
4. `dcd_ch32_usbhs.c`：`#if TUD_OPT_HIGH_SPEED` 成立 → 使用 `USBHS_HIGH_SPEED` 模式

**结论**：`OPT_MODE_DEFAULT_SPEED` (0x0000) 表示"使用 MCU 支持的最高速度"，对 CH32V307 就是 High-Speed 480Mbps。参考项目显式写 `OPT_MODE_HIGH_SPEED` 只是更明确，效果完全相同。

#### 0.4 .cproject 添加 Include 路径

在 `.cproject` 的 C Compiler → Include paths 中添加：
```
${workspace_loc:/${ProjName}/bsp}
```

> `CFG_TUSB_MCU` 已在 `tusb_config.h` 中通过 `#define` 定义，不需要在 `.cproject` 中重复添加。

在 `.cproject` 的 C Compiler → Include paths 中添加：
```
${workspace_loc:/${ProjName}/bsp}
```

#### 0.5 创建 bsp/ 目录结构

需要新建的文件（从参考项目复制）：
```
bsp/
├── board.h              # 引脚定义 (LED_PORT, LED_PIN 等)
├── board_api.h          # Board API 声明 (board_init, board_millis 等)
├── board_init.h         # 初始化头文件
├── board_init.c         # USBHS 时钟、SysTick、LED、USB 中断转发
├── joystick.h           # 摇杆 API
└── joystick.c           # ADC 摇杆驱动
```

#### 0.6 验证标准

- 编译无错误无警告
- TinyUSB 只编译 7-9 个 .o 文件（从 74 个大幅减少）
- obj/TinyUSB 目录从 2.2MB 下降到 ~668KB
- `tusb_config.h` 中 `BOARD_TUD_MAX_SPEED` 为 `OPT_MODE_DEFAULT_SPEED`（对 CH32V307 即 High-Speed）
- `CFG_TUSB_MCU=OPT_MCU_CH32V307` 已在 `tusb_config.h` 中定义
- `bsp/` 目录已创建并可被 Include 路径找到

**预计工作量**：1 小时

---

### 阶段 1：DS4 手柄核心功能移植

**目标**：将参考项目 `CH32V307VCT_tinyusb_ds4` 的 PS4 手柄功能移植到当前项目，使 PC 识别为 Sony DualShock 4。

**参考项目已有成果**：
- `ps4_descriptors.h` — PS4 报告结构体、VID/PID (0x054C/0x09CC)、HAT 常量、Feature Report 静态响应数据
- `usb_descriptors.c` — 完整的设备/配置/HID 描述符、HID Report Descriptor（含 Game Pad 和 Auth 两个 Collection）、Feature Report GET/SET 回调
- `bsp/board_init.c` — USBHS 时钟配置（USBPHY 48MHz、PLL、PHY 使能）、SysTick 1ms 定时器、USB 中断转发到 TinyUSB
- `bsp/joystick.c` — ADC 驱动（PC0-PC3 四通道双摇杆）、GPIO 按键（PB9/PB10 摇杆按下）、原始值到 0-255 映射
- `User/main.c` — PS4 报告构建逻辑、keepalive 机制（5ms 无变化强制发送）、LED 闪烁状态指示

**操作内容**：
1. 从参考项目复制 `ps4_descriptors.h`、`usb_descriptors.c`、`usb_descriptors.h` 到 `User/`
2. 从参考项目复制 `bsp/` 目录下所有文件（`board_init.c/h`、`board.h`、`board_api.h`、`joystick.c/h`）
3. 重写 `User/main.c`，实现 PS4 报告构建、keepalive、TinyUSB 回调
4. 在 `bsp/board_init.c` 中注册 `USBHS_IRQHandler` 中断，转发到 `tud_int_handler(0)`
5. 编译烧录，在 PC 上验证

**涉及的技术要点**：
- PS4 使用标准 HID 类（0x03），不需要自定义 USB 类驱动
- HID Report Descriptor 定义了 Report ID 1（输入，64字节）、Report ID 5（输出，31字节）、多个 Feature Report（0x02-0xAF 用于设备信息查询、0xF0-0xF3 用于认证）
- PC 端不需要 PS4 认证（只有 PS4 主机才需要），Feature Report 返回静态数据即可
- 配置描述符需要手写（不能用 TinyUSB 宏），因为 DS4 需要 2 个端点（IN + OUT），而宏默认只生成 1 个
- USBHS 中断必须在启动文件的 weak 默认处理函数基础上覆盖，否则中断不会转发到 TinyUSB

**验证标准**：
- Windows 设备管理器显示 "Wireless Controller"，VID=054C，PID=09CC
- USB Tree Viewer 确认 High-Speed 480Mbps 模式
- Steam 大屏模式 → 设置 → 控制器 能识别为 PS4 手柄
- 左右摇杆 XY 轴在 Steam 控制器测试中有响应（0-255）
- L3/R3 按键按下能被检测到
- D-Pad 方向正确

**预计工作量**：2-3 小时

---

### 阶段 2：配置文件系统

**目标**：实现配置数据的定义、Flash 存储、运行时加载，支持掉电保存。

**核心设计决策**：
- 配置存储在 Flash 最后一页（256 字节快速页），地址 `0x00047F00`（读）/ `0x08047F00`（擦写）
- 使用自定义二进制格式而非 JSON（32KB RAM 不够跑 JSON 解析器）
- 使用 `FLASH_ROM_ERASE` / `FLASH_ROM_WRITE` 统一接口（内部处理解锁/锁定）
- 配置结构体包含 magic + version + checksum 三重校验

**配置项清单**：
- 按键映射：14 个 PS4 按键各自映射到一个物理输入源
- 摇杆参数：左右摇杆死区（0-50%）、灵敏度曲线（线性/二次/立方）、XY 轴反转
- 扳机参数：模式（模拟/数字）、死区
- 功能开关：LED、震动、触摸板、陀螺仪
- D-Pad 配置：输入源选择（HAT 开关 / 4 个独立按键）
- USB 模式：PS4 / XInput（预留）
- Keepalive 间隔

**操作内容**：
1. 创建 `User/config.h` — 定义 `GamepadConfig` 结构体（256 字节 packed）、输入源枚举、PS4 按键枚举
2. 创建 `User/config.c` — 实现 `config_init`（Flash 读取 + 校验）、`config_save`（擦除 + 写入）、`config_reset_to_default`、`config_validate`
3. 创建 `User/config_default.h` — 定义默认配置值和默认按键映射表
4. 在 `main.c` 中集成配置初始化流程
5. 验证首次上电自动写入默认配置、掉电重启后配置保持

**Flash 地址映射说明**：
- CH32V307 有两套地址：RISC-V 地址 `0x00000000`（CPU 读取）和 ARM 地址 `0x08000000`（Flash 控制器擦写）
- 读配置：`memcpy` 从 `0x00047F00` 直接读（无需时钟处理）
- 写配置：`FLASH_ROM_ERASE(0x08047F00, 256)` 擦除 + `FLASH_ROM_WRITE(0x08047F00, buf, 256)` 写入
- `FLASH_ROM_WRITE` 要求缓冲区为 `uint32_t*` 且 256 字节对齐
- **144MHz 注意**：Flash 访问时钟 72MHz 超过 60MHz 推荐值，但官方例程未降频也能正常工作；如遇不稳定可降频操作

**验证标准**：
- 首次上电后，Flash 未写入区域读出 `0xe339e339`（非 `0xFFFFFFFF`），magic 校验失败 → 自动写入默认配置
- 写入默认配置后，Flash `0x00047F00` 处数据的 magic 为 `0x44533443`
- 修改配置后保存，掉电重启，配置保持不变
- 手动破坏 Flash 配置区（写入随机数据），重启后自动恢复默认配置
- 校验和验证正确

**预计工作量**：2-3 小时

---

### 阶段 3：输入驱动与按键映射

**目标**：实现通用的物理输入 → PS4 按键映射引擎，支持运行时根据配置动态映射。

**输入源类型**：
- ADC 摇杆（已实现）：4 通道模拟量，映射到左右摇杆 XY
- 摇杆按键（已实现）：PB9/PB10，映射到 L3/R3
- GPIO 按键（待扩展）：可接外部按键，映射到任意 PS4 按键
- 组合键（待扩展）：多个物理按键组合触发一个 PS4 按键

**操作内容**：
1. 在 `joystick.c` 中添加死区处理函数 `apply_deadzone`（基于配置的百分比值）
2. 在 `joystick.c` 中添加灵敏度曲线函数 `apply_sensitivity`（线性/二次/立方三种曲线）
3. 在 `joystick.c` 中添加摇杆反转支持（根据配置的 invert 标志）
4. 在 `main.c` 中实现通用按键映射引擎 `apply_button_mapping`（遍历 14 个 PS4 按键，根据 `button_map[]` 查找对应的物理输入源）
5. 在 `main.c` 中实现 D-Pad 映射（支持 HAT 开关和 4 按键两种模式）
6. 在 `main.c` 的 `ps4_gamepad_task` 中串联整个流程：读取输入 → 死区 → 灵敏度 → 反转 → 按键映射 → 构建报告 → 发送

**死区算法要点**：
- 死区以百分比表示（0-50%），对应中心点附近 0-64 的偏移量
- 死区内的输出固定为 128（居中）
- 死区外的范围重新拉伸到 0-255，避免死区导致的输出范围损失
- 左右摇杆死区独立配置

**灵敏度曲线要点**：
- 线性：output = input（默认，无变换）
- 二次：output = sign(input) × input²（中心区域更精细，边缘更灵敏）
- 立方：output = input³（极端非线性，适合精细操作）

**验证标准**：
- 死区设置为 10% 时，摇杆中心区域 ±12 的偏移不产生输出变化
- 灵敏度设为二次曲线时，轻推摇杆的响应比线性模式更细腻
- 摇杆反转后，方向与物理方向相反
- 修改按键映射后（如将 L3 映射到 Cross），按下摇杆在 Steam 测试中显示为 Cross 按下
- D-Pad 从 HAT 开关模式切换到 4 按键模式后，方向控制正常

**预计工作量**：2-3 小时

---

### 阶段 4：PC 端配置工具

**目标**：提供 PC 端工具，让用户通过可视化界面编辑配置，生成二进制文件并上传到设备。

**工具形态**：
- Python 命令行脚本（第一优先级，快速实现）
- Web HID 页面（第二优先级，用户体验更好）
- 桌面 GUI 应用（远期，可选）

**操作内容**：
1. 创建 `Tools/config_tool.py` — 实现 JSON ↔ 二进制配置的双向转换
2. 生成默认配置 JSON 模板（带注释，用户友好）
3. 实现校验和计算和验证
4. 实现配置上传方案（二选一或两者都支持）：
   - 方案 A：通过 USB HID Feature Report 上传（需要在设备端添加自定义 Report ID 0xC0 的 SET_REPORT 处理）
   - 方案 B：通过串口上传（简单，需要 USB-TTL 模块连接 USART1）
5. 编写用户文档：JSON 配置字段说明、使用教程

**JSON 配置模板结构**：
- `button_map`：14 个按键的映射关系（字符串枚举，如 "joy_l3"、"none"）
- `left_stick` / `right_stick`：死区、反转、灵敏度
- `trigger`：模式、死区
- `features`：LED、震动、触摸板、陀螺仪开关
- `dpad`：输入源、4 个方向的映射
- `usb`：模式选择

**验证标准**：
- `python config_tool.py default.json` 生成 256 字节二进制文件
- 二进制文件的 magic、version、checksum 正确
- 生成的二进制文件可以直接写入 Flash `0x00047F00` 并被设备正确读取
- JSON → 二进制 → JSON 往返转换后数据一致

**预计工作量**：2-3 小时（Python 脚本），8-16 小时（Web HID）

---

### 阶段 5：扩展功能

**目标**：在基础手柄功能稳定后，逐步添加增强功能。

**功能列表（按优先级排序）**：

| 优先级 | 功能 | 说明 | 依赖 |
|--------|------|------|------|
| P1 | GPIO 外部按键 | 在按键映射中添加 GPIO 输入源，支持接线按键 | 无 |
| P1 | 运行时配置切换 | 按住特定按键组合上电，进入配置模式或恢复默认 | 配置系统 |
| P2 | LED 状态指示 | 用 RGB LED 表示手柄状态（连接/断开/配置模式） | GPIO |
| P2 | 触摸板模拟 | 将右摇杆或物理触摸模块映射为 PS4 触摸板 | HID 描述符 |
| P2 | OLED 显示屏 | I2C SSD1306 显示当前配置、按键状态、连接状态 | I2C 驱动 |
| P3 | USB 震动反馈 | 接收主机的 LED/震动 Output Report，驱动电机 | HID OUT 端点 |
| P3 | XInput 模式 | 运行时切换为 Xbox 360 手柄协议，兼容更多游戏 | 自定义 USB 类驱动 |
| P3 | 多手柄支持 | 同一设备模拟多个手柄（需要多接口 HID） | USB 描述符 |
| P4 | PS4 认证 | 实现 PS4 主机的加密握手（需要提取密钥） | 加密算法 |
| P4 | PS5 DualSense | 模拟 PS5 手柄（更复杂的认证和协议） | PS5 描述符 |
| P4 | 无线连接 | 蓝牙或 2.4G 无线模块 | 硬件改造 |

**XInput 模式切换方案**：
- 通过配置中的 `usb_mode` 字段选择 PS4 或 XInput
- 切换时需要 USB 断开重连（改变 VID/PID 和描述符）
- XInput 使用 Vendor Specific 类（0xFF），不需要 HID Report Descriptor
- XInput 没有认证要求，兼容性最好

**验证标准**：各功能独立验证，不影响基础手柄功能的稳定性。

**预计工作量**：每个功能 2-8 小时不等

---

### 阶段 6：稳定性与量产

**目标**：确保长期使用的稳定性和可靠性，准备量产。

**操作内容**：
1. 长时间压力测试：连续运行 24+ 小时，监控是否有 USB 断连、数据异常
2. 热插拔测试：反复插拔 USB，确认每次都能正确枚举
3. 兼容性测试：在多台 PC、多个操作系统（Win10/Win11/Linux/macOS）、多个游戏上测试
4. Flash 耐久性测试：反复写入配置 10000+ 次，确认 Flash 不损坏
5. 异常恢复测试：在 Flash 写入过程中断电，确认设备能恢复
6. 电流测试：确认 USB 电流不超过 100mA（USB 规范要求）
7. ESD 防护：添加 TVS 二极管保护 USB 数据线
8. PCB 设计：如果需要定制 PCB，考虑 USB 阻抗匹配、晶振布局

**量产准备**：
- 编写烧录脚本（一键烧录固件 + 默认配置）
- 制作烧录治具（如果批量生产）
- 编写用户手册
- 建立固件版本管理（配置文件 version 字段同步更新）

**预计工作量**：1-2 周

---

### 时间线总览

```
第 1 天    阶段 0 工程瘦身 ──────────────────────┐
第 1-2 天  阶段 1 DS4 核心移植 ──────────────────┤ 基础可用
第 2-3 天  阶段 2 配置文件系统 ──────────────────┤
第 3-5 天  阶段 3 输入驱动与映射 ────────────────┘
第 5-7 天  阶段 4 PC 配置工具 ──────────────────── 用户体验
第 7-14 天 阶段 5 扩展功能 (按需) ─────────────── 功能丰富
第 14+ 天  阶段 6 稳定性与量产 ────────────────── 可靠量产
```

---

### 关键风险与应对

| 风险 | 影响 | 应对 |
|------|------|------|
| TinyUSB HID 描述符长度限制 | 可能无法放入完整的 PS4 HID Report Descriptor | TinyUSB 默认 256 字节，PS4 描述符约 600 字节，需修改 `CFG_TUD_HID_DESCRIPTOR_DEFERRED_MODE_MAX_SIZE` 或使用分段传输 |
| USBHS 驱动与 TinyUSB 版本兼容性 | WCH 移植驱动可能不完全适配当前 TinyUSB 版本 | 参考项目使用 TinyUSB 0.17.x，当前项目子模块可能版本不同，需验证或降级 |
| Flash 写入中断电导致配置损坏 | 设备无法启动或行为异常 | magic + checksum 双重校验，损坏时自动恢复默认配置 |
| 144MHz 下 Flash 访问时钟超出推荐值 | Flash 访问时钟 72MHz 超过 60MHz 推荐值 | 官方例程在 144MHz 下未降频也能正常工作；如遇不稳定可在 config_save() 中降频 |
| PS4 Feature Report 响应不完整 | 主机可能拒绝识别设备或限制功能 | 参考项目已验证 PC 端可用，保持 Feature Report 数据不变 |
| 32KB RAM 限制 | 复杂功能可能内存不足 | 优化缓冲区大小，使用 Flash 直接读取减少 RAM 占用 |

---

### 附：Bootloader 项目分析 (CH32V307VCT_BOOTLOADER)

参考项目 `CH32V307VCT_tinyusb_ds4` 配套有一个 WebUSB Bootloader，用于通过浏览器刷写固件。

#### Bootloader Flash 布局

```
Flash 地址 (RISC-V)        大小      用途
0x00000000 - 0x00004FFF    20KB      Bootloader 固件
0x00005000 - 0x00047EFF    268KB     应用固件 (DS4 手柄)
0x00047F00 - 0x00047FFF    256B      IAP 标志页 ⚠️
```

#### IAP 标志页详情

Bootloader 在 Flash 最后一页 (`0x00047F00`) 写入一个有效应用标志：

| 偏移 | 大小 | 内容 | 说明 |
|------|------|------|------|
| 0x00 | 4 | `0x5AA55AA5` | IAP 有效标志 (magic) |
| 0x04 | 4 | CRC32 | 固件校验 (预留) |
| 0x08 | 4 | 固件大小 | 固件字节数 (预留) |

**⚠️ 关键冲突**：Bootloader 的 IAP 标志页地址 `0x00047F00` 与我们规划的配置存储地址**完全重叠**！

#### Bootloader 启动流程

```
上电
 │
 ▼
检查 PA2 按键 (50ms 去抖)
 ├─ 按下 → 进入 Bootloader 模式 (LED 快闪 200ms)
 └─ 未按下 → 检查 IAP 标志
     ├─ IAP 标志 == 0x5AA55AA5 → 跳转到应用 (0x5000)
     ├─ Flash 首字 != 0xFFFFFFFF 且 != 0xE339E339 → 跳转到应用
     └─ 无有效应用 → 进入 Bootloader 模式
```

#### 跳转机制

Bootloader 通过 RISC-V 软件中断跳转到应用：

1. 禁用 USBHS 中断
2. 反初始化 GPIO 和外设时钟
3. 触发 `Software_IRQn`
4. `SW_Handler` 执行 `jr 0x5000` 跳转到应用

**注意**：不能在跳转前调用 `__disable_irq()`，因为软件中断本身也需要全局中断使能才能触发。

#### IAP 通信协议

| 层级 | 说明 |
|------|------|
| 传输层 | USB Bulk EP2 (OUT=命令, IN=响应), VID=0x1A86/PID=0x7F01 |
| 帧格式 | `[0xAA][0x55][CMD][LEN][DATA...][CKSUM_LO][CKSUM_HI][0x55][0xAA]` |
| 命令 | ERASE(0x81) → PROM(0x80) → VERIFY(0x82) → END(0x83) |
| Flash 操作 | 使用快速页编程 (256 字节/页)，操作期间禁用中断 |

#### Web Flasher

Bootloader 配套一个 WebUSB 网页刷写工具 (`web/flasher.html`)，支持：
- 浏览器内直接连接设备 (WebUSB API)
- 拖拽或输入 URL 加载 .bin 固件文件
- 实时进度条和日志输出
- 自动发送 ERASE → PROM → VERIFY → END 命令序列

#### 配置区冲突解决方案

由于 Bootloader IAP 标志和我们的配置区都使用 Flash 最后一页，有以下方案：

| 方案 | 做法 | 优缺点 |
|------|------|--------|
| **A: 共享页面** | IAP 标志占前 12 字节，配置占后 244 字节 | 简单，但需要调整配置结构体大小 |
| **B: 使用倒数第二页** | 配置区改为 `0x00047E00` (256 字节) | 无冲突，但需要修改链接脚本确保固件不超过 264KB |
| **C: 不使用 Bootloader** | 当前项目不集成 Bootloader，配置用最后一页 | 最简单，适合开发阶段 |
| **D: 配置嵌入固件** | 配置编译时写入 .rodata，不单独存储 | 无法运行时修改，需重新烧录 |

**推荐方案 C**（开发阶段）或 **方案 A**（集成 Bootloader 时）。

---

## 1. 目标概述

在当前项目 `CH32V307VCT_tinyusb_ds4_A` 的基础上，实现：

1. **PS4 DualShock 4 手柄功能** — PC 识别为 Sony DS4，支持 Steam/Apex 等游戏
2. **配置文件系统** — 通过 JSON 配置文件自定义按键映射、摇杆死区、功能开关等
3. **配置持久化** — 配置存储在 Flash 中，掉电不丢失

---

## 2. 参考项目分析

参考项目 `CH32V307VCT_tinyusb_ds4` 已完成的核心工作：

| 模块 | 文件 | 说明 |
|------|------|------|
| PS4 描述符 | `User/ps4_descriptors.h` | PS4Report 结构体、VID/PID、HAT 常量、Feature Report 静态数据 |
| USB 描述符 | `User/usb_descriptors.c` | 设备/配置/HID 描述符、HID Report Descriptor、Feature Report 回调 |
| 摇杆驱动 | `bsp/joystick.c` | ADC 读取 PC0-PC3 (双摇杆 XY)、PB9/PB10 (L3/R3 按键) |
| 板级初始化 | `bsp/board_init.c` | USBHS 时钟配置、SysTick、USB 中断转发到 TinyUSB |
| 主程序 | `User/main.c` | PS4 报告构建、keepalive 机制、LED 闪烁 |
| 链接脚本 | `Ld/Link.ld` | 偏移 0x5000 (Bootloader 预留 20KB) |

**参考项目的关键差异**：
- 使用 `OPT_MODE_HIGH_SPEED`（效果与当前项目的 `OPT_MODE_DEFAULT_SPEED` 相同，都是 HS 480Mbps）
- 链接地址偏移 `0x5000`（有 Bootloader）
- 仅编译 TinyUSB HID 设备类（精简体积）

---

## 3. 配置文件系统设计

### 3.1 存储方案

CH32V307VCT 有 288KB Flash。配置文件存储在 Flash 的**最后一页**。

| 参数 | 值 |
|------|-----|
| Flash 快速页大小 | 256 字节 (`FLASH_ErasePage_Fast` / `FLASH_ProgramPage_Fast`) |
| Flash 标准页大小 | 4KB (`FLASH_ErasePage`，擦除粒度更大) |
| 配置存储地址 (读) | `0x00047F00` (RISC-V 地址，CPU 直接读取) |
| 配置存储地址 (擦写) | `0x08047F00` (ARM 地址，Flash 控制器操作) |
| 配置数据大小 | ≤ 256 字节 (1 个快速页) |
| 格式 | 自定义二进制格式 (非 JSON，节省空间) |

> **CH32V307 Flash 地址映射说明**：
> - CPU 执行代码和读取数据使用 `0x00000000` 起始地址（启动后 remap）
> - Flash 控制器擦写操作必须使用 `0x08000000` 起始地址
> - `FLASH_ROM_ERASE()` 和 `FLASH_ROM_WRITE()` 内部校验地址范围为 `0x08000000 - 0x08078000`
> - 配置区在 Flash 最后 256 字节：RISC-V 读 `0x00047F00`，擦写用 `0x08047F00`

> **为什么不用 JSON？**
> - CH32V307 只有 32KB RAM，JSON 解析器占用 ~2-4KB RAM + ~6-10KB Flash
> - 配置项固定且有限，二进制格式更紧凑
> - 通过 PC 端工具 (Python 脚本) 生成二进制配置文件
> - 通过 USB HID Feature Report 或自定义 USB 接口上传配置

### 3.2 配置数据结构

```c
// User/config.h

#define CONFIG_MAGIC            0x44533443  // "DS4C"
#define CONFIG_VERSION          0x01
#define CONFIG_FLASH_ADDR_READ  0x00047F00  // RISC-V 地址 (CPU 读取)
#define CONFIG_FLASH_ADDR_WRITE 0x08047F00  // ARM 地址 (Flash 擦写)
#define CONFIG_FLASH_SIZE       256         // 字节 (1 个快速页)

// 按键映射: 每个逻辑按键 -> 物理输入源
typedef enum {
    INPUT_NONE        = 0x00,
    INPUT_BTN_PC0     = 0x01,  // GPIO 按键 (可扩展)
    INPUT_BTN_PC1     = 0x02,
    INPUT_ADC_CH10    = 0x10,  // ADC 通道
    INPUT_ADC_CH11    = 0x11,
    INPUT_ADC_CH12    = 0x12,
    INPUT_ADC_CH13    = 0x13,
    INPUT_JOY_L3      = 0x20,  // 摇杆按键
    INPUT_JOY_R3      = 0x21,
} InputSource;

// PS4 按键目标
typedef enum {
    PS4_BTN_CROSS     = 0,
    PS4_BTN_CIRCLE    = 1,
    PS4_BTN_SQUARE    = 2,
    PS4_BTN_TRIANGLE  = 3,
    PS4_BTN_L1        = 4,
    PS4_BTN_R1        = 5,
    PS4_BTN_L2        = 6,
    PS4_BTN_R2        = 7,
    PS4_BTN_SELECT    = 8,
    PS4_BTN_START     = 9,
    PS4_BTN_L3        = 10,
    PS4_BTN_R3        = 11,
    PS4_BTN_PS        = 12,
    PS4_BTN_TOUCHPAD  = 13,
    PS4_BTN_COUNT     = 14,
} PS4ButtonId;

// 方向键映射 (D-Pad)
typedef enum {
    DPAD_SOURCE_HAT_SWITCH = 0,  // 物理 HAT 开关
    DPAD_SOURCE_BUTTONS    = 1,  // 4 个独立按键
} DpadSource;

typedef struct __attribute__((packed)) {
    uint32_t magic;               // 0x44533443 "DS4C"
    uint8_t  version;             // 配置版本
    uint8_t  checksum;            // 前 N 字节校验和

    // === 按键映射 (14 个 PS4 按键) ===
    uint8_t  button_map[PS4_BTN_COUNT];  // InputSource 枚举值

    // === 摇杆配置 ===
    uint8_t  deadzone_left;       // 左摇杆死区 (0-50, 默认 5)
    uint8_t  deadzone_right;      // 右摇杆死区 (0-50, 默认 5)
    uint8_t  stick_sensitivity;   // 灵敏度曲线 (0=线性, 1=二次, 2=立方)
    uint8_t  invert_left_x;       // 左摇杆 X 轴反转
    uint8_t  invert_left_y;       // 左摇杆 Y 轴反转
    uint8_t  invert_right_x;      // 右摇杆 X 轴反转
    uint8_t  invert_right_y;      // 右摇杆 Y 轴反转

    // === 触发器配置 ===
    uint8_t  trigger_mode;        // 0=模拟, 1=数字 (按键模式)
    uint8_t  trigger_deadzone;    // 扳机死区 (0-50)

    // === 功能开关 ===
    uint8_t  enable_rumble;       // 震动反馈 (保留)
    uint8_t  enable_led;          // LED 指示灯
    uint8_t  enable_touchpad;     // 触摸板模拟
    uint8_t  enable_gyro;         // 陀螺仪模拟 (保留)
    uint8_t  keepalive_ms;        // Keepalive 间隔 (默认 5ms)

    // === D-Pad 配置 ===
    uint8_t  dpad_source;         // DpadSource
    uint8_t  dpad_btn_up;         // D-Pad 上 -> InputSource
    uint8_t  dpad_btn_down;       // D-Pad 下
    uint8_t  dpad_btn_left;       // D-Pad 左
    uint8_t  dpad_btn_right;      // D-Pad 右

    // === USB 配置 ===
    uint8_t  usb_mode;            // 0=PS4, 1=XInput (预留)
    uint8_t  usb_vid_pid_override; // 是否覆盖 VID/PID

    // === 保留区域 ===
    uint8_t  reserved[208];       // 填充到 256 字节

} GamepadConfig;
```

### 3.3 配置管理流程

```
上电
 │
 ▼
config_init()
 │  1. 从 Flash 读取配置
 │  2. 校验 magic + version + checksum
 │  3. 无效 → 加载默认配置
 │  4. 有效 → 使用存储的配置
 ▼
应用层使用配置
 │  - 按键映射
 │  - 死区处理
 │  - 功能开关
 ▼
运行时修改配置 (可选)
 │  通过 USB HID Feature Report 接收新配置
 │  写入 Flash
 ▼
下次上电生效
```

---

## 4. 文件架构设计

### 4.1 需要新建的文件

```
User/
├── config.h              # 配置数据结构定义
├── config.c              # 配置读写、Flash 操作、默认值
├── ps4_descriptors.h     # PS4 描述符 (从参考项目复制)
├── usb_descriptors.c     # USB 描述符 (从参考项目复制并适配)
├── usb_descriptors.h     # USB 描述符头文件
└── main.c                # 主程序 (重写)

bsp/
├── board_init.h          # 板级初始化头文件
├── board.h               # 板级引脚定义
├── board_api.h           # board API 声明
├── board_init.c          # 板级初始化 (USBHS 时钟、SysTick、GPIO)
├── joystick.h            # 摇杆头文件
└── joystick.c            # 摇杆 ADC 驱动
```

### 4.2 文件职责

| 文件 | 职责 | 行数估计 |
|------|------|----------|
| `User/config.h` | 配置结构体、枚举、默认值宏 | ~120 |
| `User/config.c` | Flash 读写、校验、默认配置加载、配置应用 | ~200 |
| `User/ps4_descriptors.h` | PS4Report、VID/PID、HAT 常量、Feature Report 数据 | ~175 |
| `User/usb_descriptors.c` | 设备/配置/HID 描述符、HID Report Descriptor、回调 | ~560 |
| `User/usb_descriptors.h` | 包含 ps4_descriptors.h | ~15 |
| `User/main.c` | 初始化、主循环、PS4 报告构建、配置应用 | ~250 |
| `bsp/board_init.c` | USBHS 时钟、SysTick、LED、USB 中断 | ~140 |
| `bsp/board.h` | LED/按键引脚定义 | ~20 |
| `bsp/joystick.c` | ADC 摇杆读取 + 死区处理 | ~200 |
| `bsp/joystick.h` | 摇杆数据结构和 API | ~50 |

---

## 5. 实现步骤

### 第一阶段：移植参考项目基础功能

**步骤 1.1** — 复制参考项目的核心文件到当前项目

从 `CH32V307VCT_tinyusb_ds4` 复制：
- `User/ps4_descriptors.h` → 当前项目 `User/`
- `User/usb_descriptors.c` → 当前项目 `User/`
- `User/usb_descriptors.h` → 当前项目 `User/`
- `bsp/` 整个目录 → 当前项目 `bsp/`

**步骤 1.2** — 确认 `tusb_config.h`（当前配置已正确，无需修改）

```c
// 已正确配置:
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED  // CH32V307 自动使用 HS
#define CFG_TUSB_MCU OPT_MCU_CH32V307                 // 已定义
```

**步骤 1.3** — 修改 `.cproject` 编译配置

添加 Include 路径：
```
${workspace_loc:/${ProjName}/bsp}
```

添加预定义符号：
```
CFG_TUSB_MCU=OPT_MCU_CH32V307
```

排除不需要的 TinyUSB 源文件（只保留 HID、device、common、portable/wch）。

**步骤 1.4** — 重写 `main.c`

参考参考项目的 `main.c`，实现：
- `board_init()` 调用
- `joystick_init()` 调用
- `tud_init()` + `tud_task()` 主循环
- PS4 报告构建和发送
- Keepalive 机制

**步骤 1.5** — 注册 USB 中断

在 `bsp/board_init.c` 中添加 `USBHS_IRQHandler`，转发到 `tud_int_handler(0)`。

**步骤 1.6** — 验证基本功能

编译烧录后，在 PC 上确认：
- 设备管理器显示 "Wireless Controller" (Sony)
- Steam 大屏模式能识别为 PS4 手柄
- 摇杆和按键有响应

---

### 第二阶段：实现配置文件系统

**步骤 2.1** — 创建 `User/config.h`

定义 `GamepadConfig` 结构体（如 3.2 节所示）。

**步骤 2.2** — 创建 `User/config.c`

实现以下函数：

```c
// 初始化配置（从 Flash 加载或使用默认值）
void config_init(void);

// 获取当前配置（只读指针）
const GamepadConfig* config_get(void);

// 保存配置到 Flash
bool config_save(const GamepadConfig* cfg);

// 恢复出厂配置
void config_reset_to_default(void);

// 校验配置完整性
bool config_validate(const GamepadConfig* cfg);

// 应用配置到运行时（更新死区、映射等）
void config_apply(const GamepadConfig* cfg);
```

`config.c` 核心实现示例：

```c
#include "ch32v30x.h"
#include "config.h"
#include "config_default.h"
#include <string.h>

static GamepadConfig current_cfg;  // RAM 中的当前配置副本

// 计算校验和 (magic/version/checksum 字段不计入)
static uint8_t calc_checksum(const GamepadConfig* cfg)
{
    uint8_t sum = 0;
    const uint8_t* p = (const uint8_t*)cfg;
    for (int i = 6; i < CONFIG_FLASH_SIZE; i++) {
        sum += p[i];
    }
    return sum;
}

void config_init(void)
{
    // 从 Flash 读取 (RISC-V 地址，CPU 直接读)
    memcpy(&current_cfg, (void*)CONFIG_FLASH_ADDR_READ, sizeof(GamepadConfig));

    // 校验 magic + version + checksum
    if (current_cfg.magic != CONFIG_MAGIC ||
        current_cfg.version != CONFIG_VERSION ||
        current_cfg.checksum != calc_checksum(&current_cfg))
    {
        // Flash 无效，加载默认配置
        config_reset_to_default();
        config_save(&current_cfg);
    }
}

const GamepadConfig* config_get(void)
{
    return &current_cfg;
}

bool config_save(const GamepadConfig* cfg)
{
    // 对齐到 256 字节的缓冲 (FLASH_ROM_WRITE 要求 uint32_t*)
    uint32_t buf[CONFIG_FLASH_SIZE / 4];
    memcpy(buf, cfg, CONFIG_FLASH_SIZE);

    // 可选: 144MHz 下降频以提高 Flash 操作可靠性
    // 官方例程在 144MHz 未降频也能工作，如遇问题再启用
    // FLASH_Access_Clock_Cfg(FLASH_Access_SYSTEM_HALF);

    // 擦除 (ARM 地址 0x08047F00)
    FLASH_Status status = FLASH_ROM_ERASE(CONFIG_FLASH_ADDR_WRITE, CONFIG_FLASH_SIZE);
    if (status != FLASH_COMPLETE) return false;

    // 写入 (ARM 地址 0x08047F00)
    status = FLASH_ROM_WRITE(CONFIG_FLASH_ADDR_WRITE, buf, CONFIG_FLASH_SIZE);
    if (status != FLASH_COMPLETE) return false;

    // 可选: 恢复 Flash 访问时钟
    // FLASH_Access_Clock_Cfg(FLASH_Access_SYSTEM);

    // 更新 RAM 副本
    memcpy(&current_cfg, cfg, sizeof(GamepadConfig));
    return true;
}

void config_reset_to_default(void)
{
    memset(&current_cfg, 0, sizeof(GamepadConfig));
    current_cfg.magic = CONFIG_MAGIC;
    current_cfg.version = CONFIG_VERSION;
    current_cfg.deadzone_left = DEFAULT_DEADZONE_LEFT;
    current_cfg.deadzone_right = DEFAULT_DEADZONE_RIGHT;
    current_cfg.keepalive_ms = DEFAULT_KEEPALIVE_MS;
    memcpy(current_cfg.button_map, default_button_map, PS4_BTN_COUNT);
    current_cfg.checksum = calc_checksum(&current_cfg);
}

bool config_validate(const GamepadConfig* cfg)
{
    if (cfg->deadzone_left > 50 || cfg->deadzone_right > 50) return false;
    if (cfg->keepalive_ms < 1 || cfg->keepalive_ms > 100) return false;
    if (cfg->stick_sensitivity > 2) return false;
    // 可添加更多校验...
    return true;
}
```

> **注意**：`CONFIG_FLASH_SIZE` 只有 256 字节，`buf[64]` (uint32_t) 仅占 256 字节栈空间，对 32KB RAM 完全无压力。

Flash 操作关键代码（推荐使用 `FLASH_ROM_ERASE` / `FLASH_ROM_WRITE` 统一接口）：

```c
// 推荐: 统一擦写接口 (内部处理解锁/锁定，256字节对齐)
FLASH_Status FLASH_ROM_ERASE(uint32_t StartAddr, uint32_t Length);
FLASH_Status FLASH_ROM_WRITE(uint32_t StartAddr, uint32_t *pbuf, uint32_t Length);

// 低级快速接口 (需要手动解锁/锁定)
void FLASH_Unlock_Fast(void);
void FLASH_Lock_Fast(void);
void FLASH_ErasePage_Fast(uint32_t Page_Address);        // 256 字节页
void FLASH_ProgramPage_Fast(uint32_t Page_Address, uint32_t *pbuf);  // 256 字节

// 标准接口 (4KB 页擦除，粒度太大不推荐用于配置存储)
FLASH_Status FLASH_ErasePage(uint32_t Page_Address);     // 4KB 页
FLASH_Status FLASH_ProgramHalfWord(uint32_t Address, uint16_t Data);  // 2 字节
```

**步骤 2.3** — 创建 `User/config_default.h`

定义默认配置值：

```c
#define DEFAULT_DEADZONE_LEFT       5
#define DEFAULT_DEADZONE_RIGHT      5
#define DEFAULT_KEEPALIVE_MS        5
#define DEFAULT_STICK_SENSITIVITY   0  // 线性
#define DEFAULT_TRIGGER_MODE        0  // 模拟

// 默认按键映射: L3按键->L1, R3按键->R1
static const uint8_t default_button_map[PS4_BTN_COUNT] = {
    INPUT_NONE,       // Cross
    INPUT_NONE,       // Circle
    INPUT_NONE,       // Square
    INPUT_NONE,       // Triangle
    INPUT_JOY_L3,     // L1 <- 左摇杆按键
    INPUT_JOY_R3,     // R1 <- 右摇杆按键
    INPUT_NONE,       // L2
    INPUT_NONE,       // R2
    INPUT_NONE,       // Select
    INPUT_NONE,       // Start
    INPUT_JOY_L3,     // L3
    INPUT_JOY_R3,     // R3
    INPUT_NONE,       // PS
    INPUT_NONE,       // Touchpad
};
```

**步骤 2.4** — 在 `main.c` 中集成配置

```c
int main(void)
{
    // ...
    config_init();
    const GamepadConfig* cfg = config_get();

    joystick_init();
    joystick_set_deadzone(cfg->deadzone_left, cfg->deadzone_right);

    tud_init(BOARD_TUD_RHPORT);

    while(1)
    {
        tud_task();
        led_blinking_task();
        ps4_gamepad_task(cfg);  // 传入配置
    }
}
```

**步骤 2.5** — 按键映射引擎

在 `ps4_gamepad_task()` 中实现通用按键映射：

```c
static void apply_button_mapping(PS4Report* report, const GamepadConfig* cfg,
                                  const JoystickState* joy_l, const JoystickState* joy_r)
{
    // 清除所有按键
    uint16_t buttons = 0;
    uint8_t dpad = PS4_HAT_NOTHING;

    // 遍历每个 PS4 按键，检查对应的输入源
    for (int i = 0; i < PS4_BTN_COUNT; i++) {
        uint8_t source = cfg->button_map[i];
        bool pressed = false;

        switch (source) {
            case INPUT_JOY_L3: pressed = joy_l->btn; break;
            case INPUT_JOY_R3: pressed = joy_r->btn; break;
            // 可扩展: GPIO 按键、ADC 阈值等
            default: break;
        }

        if (pressed) buttons |= (1U << i);
    }

    // D-Pad 映射
    if (cfg->dpad_source == DPAD_SOURCE_BUTTONS) {
        if (read_input(cfg->dpad_btn_up))    dpad = PS4_HAT_UP;
        if (read_input(cfg->dpad_btn_down))  dpad = PS4_HAT_DOWN;
        // ... 组合方向
    }

    // 写入报告
    *(uint16_t*)&report->dpad = (buttons << 4) | (dpad & 0x0F);
}
```

---

### 第三阶段：PC 端配置工具

**步骤 3.1** — Python 配置生成工具

创建 `Tools/config_tool.py`：

```python
import struct
import json

class DS4Config:
    MAGIC = 0x44533443  # "DS4C"
    VERSION = 1

    def __init__(self):
        self.button_map = [0] * 14
        self.deadzone_left = 5
        self.deadzone_right = 5
        # ... 其他字段

    def to_binary(self) -> bytes:
        """生成 256 字节二进制配置"""
        data = struct.pack('<I B B',
            self.MAGIC, self.VERSION, 0)  # checksum 后面计算
        data += bytes(self.button_map)
        data += struct.pack('<BBBBBBBB',
            self.deadzone_left, self.deadzone_right,
            self.stick_sensitivity, 0, 0, 0, 0, 0)
        # ... 填充到 256 字节
        checksum = sum(data[6:]) & 0xFF
        data = data[:5] + bytes([checksum]) + data[6:]
        return data

    @classmethod
    def from_json(cls, path: str) -> 'DS4Config':
        """从 JSON 文件加载配置"""
        with open(path) as f:
            d = json.load(f)
        cfg = cls()
        cfg.button_map = d.get('button_map', cfg.button_map)
        cfg.deadzone_left = d.get('deadzone_left', 5)
        # ...
        return cfg

    @classmethod
    def default_json(cls, path: str):
        """生成默认配置 JSON 模板"""
        cfg = cls()
        template = {
            "button_map": cfg.button_map,
            "deadzone_left": cfg.deadzone_left,
            "deadzone_right": cfg.deadzone_right,
            "stick_sensitivity": 0,
            "invert_left_x": False,
            "invert_left_y": False,
            "invert_right_x": False,
            "invert_right_y": False,
            "keepalive_ms": 5,
            "dpad_source": 0,
        }
        with open(path, 'w') as f:
            json.dump(template, f, indent=2)

if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("Usage: python config_tool.py <input.json> [output.bin]")
        sys.exit(1)
    cfg = DS4Config.from_json(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else 'ds4_config.bin'
    with open(out, 'wb') as f:
        f.write(cfg.to_binary())
    print(f"Written {len(cfg.to_binary())} bytes to {out}")
```

**步骤 3.2** — 配置上传方式

两种方案可选：

| 方案 | 实现难度 | 用户体验 |
|------|----------|----------|
| **A: USB HID Feature Report** | 中等 | PC 工具通过 HID 发送配置数据到设备 |
| **B: 串口上传** | 简单 | 通过 USART1 发送二进制配置 (需 USB-TTL 模块) |
| **C: 预编译** | 最简 | 编译时嵌入配置，修改需重新烧录 |

**推荐方案 A**，通过 USB HID Feature Report 实现：

```c
// 在 usb_descriptors.c 的 tud_hid_set_report_cb 中添加:
case PS4_SET_CONFIG:  // 自定义 Report ID (如 0xC0)
    if (bufsize >= sizeof(GamepadConfig)) {
        GamepadConfig* new_cfg = (GamepadConfig*)buffer;
        if (config_validate(new_cfg)) {
            config_save(new_cfg);
            // 设备会重启并加载新配置
        }
    }
    break;
```

---

## 6. 完整文件清单

### 6.1 当前项目需要新增/修改的文件

| 文件 | 操作 | 来源 |
|------|------|------|
| `User/main.c` | **重写** | 参考项目 + 配置集成 |
| `User/ps4_descriptors.h` | **新建** | 从参考项目复制 |
| `User/usb_descriptors.c` | **新建** | 从参考项目复制 + 配置上传支持 |
| `User/usb_descriptors.h` | **新建** | 从参考项目复制 |
| `User/config.h` | **新建** | 全新编写 |
| `User/config.c` | **新建** | 全新编写 |
| `User/config_default.h` | **新建** | 全新编写 |
| `User/tusb_config.h` | **已正确** | DEFAULT_SPEED 对 CH32V307 即 HS，无需修改 |
| `bsp/board_init.c` | **新建** | 从参考项目复制 |
| `bsp/board_init.h` | **新建** | 从参考项目复制 |
| `bsp/board.h` | **新建** | 从参考项目复制 |
| `bsp/board_api.h` | **新建** | 从参考项目复制 |
| `bsp/joystick.c` | **新建** | 从参考项目复制 + 死区配置支持 |
| `bsp/joystick.h` | **新建** | 从参考项目复制 |
| `.cproject` | **修改** | 添加 bsp include 路径、排除多余 TinyUSB 源文件 |
| `Ld/Link.ld` | **可选修改** | 如需 Bootloader 则偏移 0x5000 |

### 6.2 配置 JSON 模板示例

```json
{
  "comment": "DS4 Gamepad Configuration",
  "button_map": {
    "cross":     "none",
    "circle":    "none",
    "square":    "none",
    "triangle":  "none",
    "l1":        "joy_l3",
    "r1":        "joy_r3",
    "l2":        "none",
    "r2":        "none",
    "select":    "none",
    "start":     "none",
    "l3":        "joy_l3",
    "r3":        "joy_r3",
    "ps":        "none",
    "touchpad":  "none"
  },
  "dpad": {
    "source": "hat_switch",
    "up":    "none",
    "down":  "none",
    "left":  "none",
    "right": "none"
  },
  "left_stick": {
    "deadzone": 5,
    "invert_x": false,
    "invert_y": false,
    "sensitivity": "linear"
  },
  "right_stick": {
    "deadzone": 5,
    "invert_x": false,
    "invert_y": false,
    "sensitivity": "linear"
  },
  "trigger": {
    "mode": "analog",
    "deadzone": 0
  },
  "features": {
    "led": true,
    "rumble": false,
    "touchpad": false,
    "gyro": false,
    "keepalive_ms": 5
  },
  "usb": {
    "mode": "ps4"
  }
}
```

---

## 7. 配置 Flash 布局

CH32V307 有两套地址映射，同一个物理 Flash 有两个地址：

```
RISC-V 地址空间 (CPU 取指/读取)          ARM 地址空间 (Flash 控制器擦写)
┌─────────────────────────┐ 0x00000000   ┌─────────────────────────┐ 0x08000000
│  Bootloader (可选, 20KB) │              │                         │
├─────────────────────────┤ 0x00005000   │                         │
│                         │              │  同一片物理 Flash        │
│  固件代码 (最大 268KB)   │              │                         │
│                         │              │                         │
├─────────────────────────┤ 0x00047E00   ├─────────────────────────┤ 0x08047E00
│  (可选: 倒数第二页)       │              │                         │
├─────────────────────────┤ 0x00047F00   ├─────────────────────────┤ 0x08047F00
│  IAP 标志 / 配置区       │ ← CPU 读取   │  IAP 标志 / 配置区       │ ← Flash 擦写
│  (256 字节, 与Bootloader │              │  (256 字节)              │
│   IAP 标志共享或独立)     │              │                         │
├─────────────────────────┤ 0x00048000   ├─────────────────────────┤ 0x08048000
│  Flash 末尾              │              │  Flash 末尾              │
└─────────────────────────┘              └─────────────────────────┘
```

> **Bootloader 冲突说明**：如果集成 Bootloader，`0x00047F00` 被 IAP 标志占用（`0x5AA55AA5`）。
> 解决方案见上方"配置区冲突解决方案"章节。无 Bootloader 时此地址直接用于配置存储。

配置文件内部布局 (256 字节)：
```
Offset  Size  Field
0x00    4     magic (0x44533443 "DS4C")
0x04    1     version
0x05    1     checksum
0x06    14    button_map[14]
0x14    1     deadzone_left
0x15    1     deadzone_right
0x16    1     stick_sensitivity
0x17    1     invert_left_x
0x18    1     invert_left_y
0x19    1     invert_right_x
0x1A    1     invert_right_y
0x1B    1     trigger_mode
0x1C    1     trigger_deadzone
0x1D    1     enable_rumble
0x1E    1     enable_led
0x1F    1     enable_touchpad
0x20    1     enable_gyro
0x21    1     keepalive_ms
0x22    1     dpad_source
0x23    1     dpad_btn_up
0x24    1     dpad_btn_down
0x25    1     dpad_btn_left
0x26    1     dpad_btn_right
0x27    1     usb_mode
0x28    1     usb_vid_pid_override
0x29    207   reserved (填充 0x00)
总计    256
```

> **Flash 擦写 API**：
> - `FLASH_ROM_ERASE(0x08047F00, 256)` — 擦除 256 字节
> - `FLASH_ROM_WRITE(0x08047F00, pbuf, 256)` — 写入 256 字节（pbuf 为 uint32_t*）
> - 两个函数内部已处理解锁/锁定，无需手动调用 `FLASH_Unlock()`
>
> **⚠️ CH32V307 Flash 擦除后不是 0xFF！**
> 官方文档明确说明：擦除后读取值为 `0xe339e339`（word）、`0xe339`（halfword）、`0x39`（byte）。
> 这意味着首次上电时未写入过的配置区读出的 magic 为 `0xe339e339`，不等于 `0x44533443`，校验失败 → 自动加载默认配置。行为正确，但**不能用 0xFFFFFFFF 判断 Flash 是否为空**。
>
> **⚠️ 144MHz 时钟下的 Flash 操作注意事项**
> 官方例程注释：系统频率 >120MHz 时，Flash 访问时钟不应超过 60MHz。
> 本项目 144MHz，默认 `SCKMOD=0` → Flash 访问时钟 = 144/2 = 72MHz，超过 60MHz 推荐值。
> 但官方例程 `Flash_Test_Fast()` 本身也在 144MHz 下运行，**并未降频就直接调用了** `FLASH_ROM_ERASE`/`FLASH_ROM_WRITE`，实际可以工作。
> `FLASH_ROM_ERASE` / `FLASH_ROM_WRITE` 内部**不会**自动分频。
> **建议**（非强制）在 config_save() 中降频操作以提高可靠性：
> ```
> FLASH_Access_Clock_Cfg(FLASH_Access_SYSTEM_HALF);  // 可选: 降频到 36MHz
> FLASH_ROM_ERASE(addr, len);
> FLASH_ROM_WRITE(addr, buf, len);
> FLASH_Access_Clock_Cfg(FLASH_Access_SYSTEM);       // 可选: 恢复
> ```
> 如果实测不降频也能稳定读写配置，可以省略此步骤。

---

## 8. 配置运行时加载流程

```
main()
 │
 ▼
config_init()
 ├─ memcpy(&cfg, (void*)CONFIG_FLASH_ADDR_READ, sizeof(cfg))
 │  // CPU 直接从 RISC-V 地址 0x00047F00 读取
 ├─ if (cfg.magic != CONFIG_MAGIC || cfg.version != CONFIG_VERSION)
 │    → config_reset_to_default()
 │    → config_save(&default_cfg)
 ├─ else if (!config_validate(&cfg))
 │    → config_reset_to_default()
 └─ else → 使用存储的配置
 │
 ▼
joystick_set_deadzone(cfg.deadzone_left, cfg.deadzone_right)
 │
 ▼
tud_init() + 主循环
 │
 ▼
ps4_gamepad_task(&cfg)
 ├─ joystick_read(&joy_l, &joy_r)
 ├─ apply_deadzone(&joy_l, cfg.deadzone_left)   // 应用死区
 ├─ apply_deadzone(&joy_r, cfg.deadzone_right)
 ├─ apply_sensitivity(&joy_l, cfg.stick_sensitivity) // 应用灵敏度曲线
 ├─ apply_inversion(&joy_l, cfg.invert_left_x, cfg.invert_left_y)
 ├─ apply_button_mapping(&report, &cfg, &joy_l, &joy_r) // 按键映射
 ├─ keepalive check
 └─ tud_hid_report(0, &report, sizeof(report))
```

---

## 9. 死区与灵敏度算法

### 9.1 死区处理

```c
uint8_t apply_deadzone(uint8_t value, uint8_t deadzone_pct)
{
    // value: 0-255, center=128
    // deadzone_pct: 0-50 (百分比)
    int16_t offset = (int16_t)value - 128;
    int16_t deadzone = (int16_t)(128 * deadzone_pct / 100);

    if (offset > -deadzone && offset < deadzone) {
        return 128;  // 死区内 → 居中
    }

    // 重映射: 死区外的范围拉伸到 0-255
    if (offset > 0) {
        return (uint8_t)(128 + (int32_t)(offset - deadzone) * 127 / (127 - deadzone));
    } else {
        return (uint8_t)(128 + (int32_t)(offset + deadzone) * 128 / (128 - deadzone));
    }
}
```

### 9.2 灵敏度曲线

```c
uint8_t apply_sensitivity(uint8_t value, uint8_t mode)
{
    // value: 0-255
    // mode: 0=线性, 1=二次, 2=立方
    float normalized = (float)(value - 128) / 127.0f;  // -1.0 ~ +1.0
    float result;

    switch (mode) {
        case 1:  // 二次曲线 (更精细的中心区域控制)
            result = normalized * fabsf(normalized);
            break;
        case 2:  // 立方曲线
            result = normalized * normalized * normalized;
            break;
        default: // 线性
            result = normalized;
            break;
    }

    return (uint8_t)(result * 127.0f + 128.0f);
}
```

---

## 10. 扩展性考虑

### 10.1 未来可扩展的功能

| 功能 | 实现方式 | 难度 |
|------|----------|------|
| GPIO 按键输入 | 在按键映射中添加 GPIO 读取 | 低 |
| 矩阵键盘 | 扫描行列 GPIO | 中 |
| LED 灯效 | PWM 控制 RGB LED | 中 |
| 震动马达 | PWM 驱动电机 | 中 |
| 触摸板模拟 | 将摇杆映射为触摸板坐标 | 中 |
| OLED 显示屏 | I2C/SPI 驱动 SSD1306 | 中 |
| 运行时模式切换 | USB 断开重连切换 PS4/XInput | 高 |
| PS4 认证 | 实现加密握手 (需要密钥) | 极高 |

### 10.2 Bootloader 支持

如果需要 OTA 升级，可以：
1. 预留 0x0000-0x4FFF 给 Bootloader
2. 修改 `Link.ld` 偏移到 `0x5000`
3. 配置区地址不变 (Flash 最后一页)
4. Bootloader 读取配置区判断是否需要升级

---

## 11. 编译优化建议

### 11.1 精简 TinyUSB 编译

参考项目只编译了 7 个 TinyUSB 文件 (对比当前项目的 70+)。建议修改 `.cproject`：

**只保留的 TinyUSB 源文件目录**：
```
TinyUSB/src/tusb.c
TinyUSB/src/common/tusb_fifo.c
TinyUSB/src/device/usbd.c
TinyUSB/src/device/usbd_control.c
TinyUSB/src/class/hid/hid_device.c
TinyUSB/src/portable/wch/dcd_ch32_usbfs.c
TinyUSB/src/portable/wch/dcd_ch32_usbhs.c
```

### 11.2 链接优化

确保以下编译选项已启用：
```
-ffunction-sections
-fdata-sections
--gc-sections  (链接器)
-Os            (按大小优化)
```

---

## 12. 测试验证清单

### 12.1 基础功能测试

- [ ] 编译通过，无错误/警告
- [ ] 设备管理器显示 "Wireless Controller" (VID=054C, PID=09CC)
- [ ] USB Tree Viewer 确认 High-Speed 模式
- [ ] Steam 大屏模式识别为 PS4 手柄
- [ ] 左摇杆 X/Y 轴响应正确 (0-255)
- [ ] 右摇杆 X/Y 轴响应正确 (0-255)
- [ ] L3/R3 按键按下识别
- [ ] D-Pad 方向正确

### 12.2 配置功能测试

- [ ] 首次上电加载默认配置
- [ ] 修改配置后保存到 Flash
- [ ] 掉电重启后配置保持
- [ ] 死区设置生效 (中心区域无漂移)
- [ ] 摇杆反转设置生效
- [ ] 按键映射修改生效
- [ ] 恢复出厂配置功能正常
- [ ] 配置校验失败时自动加载默认值

### 12.3 游戏兼容性测试

- [ ] Apex Legends 能识别手柄
- [ ] Steam 大屏模式完整支持
- [ ] Windows 设置 → 设备 → 游戏控制器 测试面板正常

---

## 13. 项目结构总览 (完成后)

```
CH32V307VCT_tinyusb_ds4_A/
├── bsp/                        # 板级支持包
│   ├── board.h                 # 引脚定义
│   ├── board_api.h             # Board API 声明
│   ├── board_init.h            # 初始化头文件
│   ├── board_init.c            # USBHS 时钟、SysTick、LED、USB 中断
│   ├── joystick.h              # 摇杆 API
│   └── joystick.c              # ADC 摇杆驱动 + 死区
├── Core/                       # RISC-V 核心 (不变)
├── Debug/                      # 调试工具 (不变)
├── Ld/                         # 链接脚本
│   └── Link.ld                 # 可选偏移 0x5000
├── Peripheral/                 # 外设库 (不变)
├── Startup/                    # 启动文件 (不变)
├── TinyUSB/                    # TinyUSB 子模块 (不变)
├── User/                       # 用户代码
│   ├── main.c                  # ★ 主程序 (重写)
│   ├── config.h                # ★ 配置数据结构
│   ├── config.c                # ★ 配置 Flash 读写
│   ├── config_default.h        # ★ 默认配置值
│   ├── ps4_descriptors.h       # ★ PS4 描述符
│   ├── usb_descriptors.c       # ★ USB 描述符
│   ├── usb_descriptors.h       # ★ USB 描述符头文件
│   ├── tusb_config.h           # 已正确 (DEFAULT_SPEED = HS)
│   ├── ch32v30x_it.c           # 中断 (不变)
│   ├── ch32v30x_it.h
│   ├── ch32v30x_conf.h
│   ├── system_ch32v30x.c       # 时钟 (不变)
│   └── system_ch32v30x.h
├── Tools/                      # PC 端工具
│   └── config_tool.py          # ★ JSON → 二进制配置转换
├── Docs/                       # 文档
│   ├── README.md
│   └── DS4_Gamepad_Config_Plan.md  # 本文档
└── obj/                        # 编译输出
```

---

## 14. 优先级建议

| 优先级 | 任务 | 预计工作量 |
|--------|------|------------|
| P0 | 移植参考项目 DS4 基础功能 | 2-3 小时 |
| P0 | 验证 PC 识别为 PS4 手柄 | 30 分钟 |
| P1 | 实现配置数据结构和 Flash 存储 | 1-2 小时 |
| P1 | 实现死区和灵敏度算法 | 1 小时 |
| P2 | 实现按键映射引擎 | 1-2 小时 |
| P2 | PC 端 Python 配置工具 | 1-2 小时 |
| P3 | USB HID Feature Report 配置上传 | 2-3 小时 |
| P3 | OLED 显示 / LED 灯效 | 3-4 小时 |
