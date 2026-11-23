# CH32V307 DS4 8K Gamepad — v0.2.0

[English](README.md) | [简体中文](README.zh-CN.md)

A complete PS4 DualShock 4 compatible gamepad: **firmware + PCB + browser configurator**.
Built on WCH **CH32V307VCT6** (RISC-V RV32IMACXW @ 144 MHz) with **TinyUSB 0.21** on the
chip's High-Speed USB 2.0 port (480 Mbps), so the HID interrupt endpoint can run down to a
**125 µs report interval (8 kHz)**.

Web configurator: [P1nkDog/ds4-gamepad-configurator](https://github.com/P1nkDog/ds4-gamepad-configurator)

- **Firmware version**: 0.1.0 (reported over MSP)
- **Hardware version**: v0.2.0 (pin map in this document is the single source of truth)

> ⚠️ **The pin map below is authoritative for the v0.2.0 PCB and has been verified on real
> hardware.** Older documents under `Docs/` (e.g. `引脚重映射修改指南_2026-10-10.md`) are kept
> as historical archives and **contradict** this table — do not follow them.

---

## 1. Features

| Feature | Detail |
|---|---|
| Polling rate | 250 / 500 / 1000 / 2000 / 4000 / 8000 Hz, selected in the configurator (default **1000 Hz**). Implemented as HID `bInterval = 6 … 1`, so 8 kHz = 125 µs per report |
| Native DS4 identity | Report descriptor + 64-byte input report follow the DualShock 4 spec; the host sees a plain `054C:09CC` DualShock 4 with no driver, no remapper |
| USB mode at boot | Plain plug → **DS4 HID mode**; hold **Share (PB12)** while plugging in → **CDC configuration mode** (virtual serial port for the browser tool) |
| Sticks | 12-bit ADC, per-axis `min / center / max` calibration, deadzone 0–50 %, full-scale mapping outside the deadzone, saved in Flash |
| Config storage | Two 4 KB Flash sectors (`0x0803E000` / `0x0803F000`), magic `0xCF07` + CRC16-CCITT, staged write (B first, then commit A) so a power loss mid-save always leaves one valid copy |
| MSP v1 protocol | 15 commands: version query, config get/set/save/reset/status, live stick values, 5-step stick calibration, device reboot |
| Deterministic send path | Report transmission is host-token driven (endpoint-completion callback), no software pacing loop, no frame tearing |
| Diagnostics | Optional block-time profiler (`Driver/stat`) printing over the debug UART; drop-frame counter readable via MSP/log |

---

## 2. Hardware

### 2.1 MCU and key components

| Item | Detail |
|---|---|
| MCU | CH32V307VCT6 — RISC-V QingKe V4F, RV32IMACXW, 144 MHz, 288 KB Flash / 32 KB RAM (LQFP-100) |
| USB | USB 2.0 High-Speed (480 Mbps) device, on-chip HS PHY, `bcdUSB = 0x0200` |
| Sticks | Two analog sticks, sampled through ADC1 (polled, no DMA) |
| Buttons | Tactile switches on the main board + FPC key panel; one BOOT0 slide switch |
| Storage | Internal Flash, 2 × 4 KB sectors reserved for configuration |
| LED | **None. The v0.2.0 board has no user LED** — `board_led_write()` is intentionally a no-op |

### 2.2 Clock tree

| Domain | Frequency | Notes |
|---|---|---|
| HSE | 8 MHz | External crystal |
| SYSCLK / HCLK | 144 MHz | PLL = HSE × 18 |
| PCLK1 (APB1) | 72 MHz | HCLK / 2 — USART2 (debug), TIM2 (×2 = 144 MHz timer clock) |
| PCLK2 (APB2) | 72 MHz | HCLK / 2 — deliberately divided so **ADC = 72 / 8 = 9 MHz**, inside the 14 MHz datasheet limit |
| USBHS | 480 MHz | USBHS PLL from HSE/2 = 4 MHz reference |
| ADC1 | 9 MHz | `RCC_ADCCLKConfig(RCC_PCLK2_Div8)`; 71.5 + 12.5 = 84 cycles ≈ **9.3 µs per conversion**, 4 channels ≈ 37 µs per frame |

### 2.3 USB, debug and mode-select pins

| Function | Pin | Notes |
|---|---|---|
| USBHS D− | PB6 | Fixed USBHS pins, on-chip PHY |
| USBHS D+ | PB7 | |
| SWDIO | PA13 | Debug, kept alive (JTAG only disabled) |
| SWCLK | PA14 | Debug, kept alive |
| JTAG (JTDO/NJTRST/JTDI) | PB3 / PB4 / PA15 | **Released** to plain GPIO via `AFIO_PCFR1.SWJ_CFG = 010`; SWD unaffected. PB3 is reused as `BTN8` |
| Mode select | PB12 | Sampled once at boot: **low = CDC config mode**, high = DS4 HID mode. Same pin as the Share button |
| Debug UART TX | PA2 | `USART2_TX`, 115200 8N1, printf retarget (`Debug/debug.h`, `DEBUG = DEBUG_UART2`). Not broken out on the v0.2.0 board — wire it manually if you need logs |

### 2.4 Analog sticks (ADC1)

| Axis | Pin | ADC channel | Polarity |
|---|---|---|---|
| Left X (LX) | PC4 | `ADC_Channel_14` | **Inverted in firmware** (`v = 4095 - v`) |
| Left Y (LY) | PC5 | `ADC_Channel_15` | Direct |
| Right X (RX) | **PA5** | `ADC_Channel_5` | **Inverted in firmware** (`v = 4095 - v`) |
| Right Y (RY) | PA6 | `ADC_Channel_6` | Direct |

PA5/PA6 and PC4/PC5 are configured as `GPIO_Mode_AIN`. The four conversions are polled
sequentially with a bounded timeout (~400 µs guard) that falls back to the last valid sample,
so a dead ADC can never hang the main loop.

### 2.5 Buttons

**All buttons are active-low with the internal pull-up, except L1 and R1, which are active-high
with the internal pull-down** (no external pull resistor on those two nets).

| DS4 function | Net name | Pin | GPIO mode | Active level |
|---|---|---|---|---|
| D-pad Up | UP | PE9 | Input pull-up | Low |
| D-pad Down | DOWN | PE11 | Input pull-up | Low |
| D-pad Left | LEFT | PE10 | Input pull-up | Low |
| D-pad Right | RIGHT | PE8 | Input pull-up | Low |
| △ Triangle (North) | Y | PE15 | Input pull-up | Low |
| ○ Circle (East) | B | PE14 | Input pull-up | Low |
| × Cross (South) | A | PE13 | Input pull-up | Low |
| □ Square (West) | X | PB11 | Input pull-up | Low |
| **L1** | L1 | **PE7** | **Input pull-DOWN** | **High** |
| **R1** | R1 | **PB10** | **Input pull-DOWN** | **High** |
| L2 (digital) | L2 | PB0 | Input pull-up | Low |
| R2 (digital) | R2 | PB1 | Input pull-up | Low |
| L3 (left stick click) | JS2 S3/S4 | **PA7** | Input pull-up | Low |
| R3 (right stick click) | JS1 S3/S4 | PE1 | Input pull-up | Low |
| PS / Home | PS | PE12 | Input pull-up | Low |
| Share / Select | SW2 | PB12 | Input pull-up | Low (also boot-mode select) |
| Options / Start | SW4 | PB14 | Input pull-up | Low |
| Touchpad click | SW3 | PB13 | Input pull-up | Low |

> L1/R1 are digital buttons, not analog triggers. The DS4 report carries `L2/R2 = 0xFF` when
> pressed and `0x00` when released — there is no analog trigger travel on this hardware.

Buttons are debounced in firmware with a **4-frame consistency filter** (zero blocking,
0.5 ms window at 8 kHz, 4 ms at 1 kHz, 16 ms at 250 Hz).

### 2.6 Extra buttons (scanned, **not** mapped to the DS4 report)

| Bit | Pin | GPIO mode | Active level | Status |
|---|---|---|---|---|
| BTN1 … BTN7 | PD1 … PD7 | Input pull-up | Low | Scanned into the button bitmap, **not** reported |
| BTN8 | PB3 | Input pull-up | Low | Same; PB3 was JTDO, released by disabling JTAG |

`INPUT_MASK_BTN1..8` are produced by `input_read_buttons()` but `gamepad.c` does not consume
them, so they never reach the HID report. Mapping is reserved for a later hardware revision.

### 2.7 Pins explicitly **not** used

| Pin | Note |
|---|---|
| PA0 / PA2 (as LED/KEY) | Template leftovers from the v0.1.0 board — removed. PA0 is unused; PA2 is the debug UART TX |
| PB4, PB5, PA15 | Released from JTAG (PB4/PA15) or simply unused (PB5) — not routed to anything in v0.2.0 |
| PC0 – PC3, PE0, PE2 – PE6, PC10, PC12 | Old v0.1.0 button/stick pins, not connected on v0.2.0 |

---

## 3. Build & Flash

### 3.1 Toolchain

| Item | Requirement |
|---|---|
| IDE (recommended) | **MounRiver Studio 2.5.0 / 3.x** — the project ships `.cproject`, `.project`, `.wvproj` and a ready `obj/makefile` |
| Compiler | `riscv-none-embed-gcc` (xPack GNU RISC-V Embedded GCC 8.2.0, bundled with MRS) |
| Arch flags | `-march=rv32imacxw -mabi=ilp32 -Os` |
| USB stack | TinyUSB 0.21 (git submodule at `TinyUSB/`) — run `git submodule update --init --recursive` after cloning |

### 3.2 Build

The MounRiver project is pre-generated; the makefile lives in `obj/`:

```bash
cd obj
make -B all          # full rebuild
```

Expected output (firmware 0.1.0 / hardware v0.2.0):

```text
   text    data     bss     dec     hex  filename
  27372     264   11792   39428    9a04  8khz-ds4-gamepad-ch32v307.elf
```

### 3.3 Outputs

| File | Contents |
|---|---|
| `obj/8khz-ds4-gamepad-ch32v307.elf` | ELF with debug symbols, for SWD download / debugging |
| `obj/8khz-ds4-gamepad-ch32v307.hex` | Intel HEX, for WCH-LinkUtility / MounRiver flash tool |
| `obj/8khz-ds4-gamepad-ch32v307.bin` | Raw binary (27636 bytes), flash at `0x08000000` |
| `obj/8khz-ds4-gamepad-ch32v307.map` | Linker map (symbol → address) |

### 3.4 Flashing

1. **SWD (recommended)** — connect **WCH-LinkE** to `PA13 (SWDIO)`, `PA14 (SWCLK)`, `GND`, `3V3`.
   In MounRiver Studio press *Download*, or use WCH-LinkUtility to program the `.hex`.
2. **USB ISP** — set the **BOOT0** switch to the high position, replug, then use
   **WCHISPStudio** to download the `.bin`/`.hex` over USB. Restore BOOT0 to low and replug
   to run the firmware.

After flashing, the first boot loads factory defaults if the config sectors are blank; the
device enumerates as a DualShock 4 immediately.

---

## 4. Configuration & Calibration

### 4.1 Entering configuration mode

1. Unplug the gamepad.
2. **Hold the Share button (PB12)**.
3. Plug the USB cable in while holding it, then release.
4. The device enumerates as **`0331:0001` "DS4 Configurator"** — a USB CDC virtual serial port
   (no driver needed on Windows 10+/Linux/macOS).
5. Open the [web configurator](https://github.com/P1nkDog/ds4-gamepad-configurator), click
   **Connect**, and pick the serial port.

To get back to gamepad mode: unplug and replug **without** holding Share, or send the MSP
reboot command from the page.

### 4.2 MSP v1 protocol

Frame format (MSP v1, `$`-prefixed, XOR checksum):

```text
request : '$' 'M' '<' <size:1> <cmd:1> <payload:size> <crc:1>
reply   : '$' 'M' '>' <size:1> <cmd:1> <payload:size> <crc:1>
          crc = XOR of size, cmd and every payload byte
```

+ `size` ≤ 254, full frame ≤ 260 bytes.
+ Error replies also use the `>` direction byte, with `payload[0]` set to a status code —
  **check the status byte, not the direction byte**.
+ Any partial frame (no new byte for 50 ms) is discarded automatically, so a dropped connection
  can never leave the parser stuck.

| Code | Command | Payload → reply |
|---|---|---|
| `0x01` | MSP_API_VERSION | — → `[major, minor]` = `[0, 1]` |
| `0x02` | MSP_FC_VARIANT | — → ASCII `"P1NKD-DS4"` |
| `0x03` | MSP_FC_VERSION | — → `[0, 1, 0]` |
| `0x04` | MSP_REBOOT | `['R']` → `[status]`, then the device resets |
| `0x10` | MSP_CONFIG_GET | `[id]` → `[id, version, data…]` |
| `0x11` | MSP_CONFIG_SET | `[id, data…]` → `[id, status]` (RAM only, not yet in Flash) |
| `0x12` | MSP_CONFIG_SAVE | — → `[status]` (commits all dirty groups to Flash) |
| `0x13` | MSP_CONFIG_RESET | `[id]` → `[id, status]` (reset one group to defaults, RAM only) |
| `0x14` | MSP_CONFIG_STATUS | — → `[count][id, size_lo, size_hi] × count` |
| `0x15` | MSP_JOYSTICK_RAW | — → `[4][raw LX,LY,RX,RY][mapped LX,LY,RX,RY]`, all u16 LE |
| `0x16` | MSP_CALIB_START | — → `[status]` |
| `0x17` | MSP_CALIB_SAMPLE | — → `[4][center LX,LY,RX,RY]`, u16 LE |
| `0x18` | MSP_CALIB_TRACK | — → `[status]` |
| `0x19` | MSP_CALIB_END | — → `[status][4][min,center,max ×4]` |
| `0x1A` | MSP_CALIB_CANCEL | — → `[status]` |

Status codes: `0` OK · `1` generic error · `2` invalid id · `3` invalid data · `4` flash error.

### 4.3 Configuration groups

| ID | Group | Size | Fields | Validation |
|---|---|---|---|---|
| 1 | `joystick_calib` | 24 B | 4 × (`min`, `center`, `max`) u16 | `min < center < max ≤ 4095` for every axis |
| 2 | `gamepad_settings` | 2 B | `deadzone` (%) | `0 … 50` |
| 3 | `polling_rate` | 1 B | `level` | `0 … 5` (250 / 500 / 1k / 2k / 4k / 8k Hz) |

Illegal values are rejected **before** they reach RAM (`MSP_STATUS_INVALID_DATA`), and a save is
refused outright if any group is currently invalid, so corrupted parameters can never be written
into Flash under a valid CRC.

### 4.4 Stick calibration

Five steps, each driven from the page:

1. `CALIB_START` — snapshots the current calibration (rollback point), state → `START`.
2. **Centre the stick**, then `CALIB_SAMPLE` — averages 16 frames per axis, state → `CENTER_SAMPLED`.
   During this state the axis output is forced to mid-scale, so the preview never shows a
   half-range jump while `min/max` are still the old values.
3. `CALIB_TRACK` — state → `TRACKING`; the firmware then tracks per-axis min/max in RAM only
   (the active config keeps the previous valid calibration until the end).
4. **Roll both sticks to all extremes**, then `CALIB_END` — checks `min < center < max` and a
   minimum travel of 200 ADC counts per side, runs the config validator, and only then marks the
   group dirty and writes Flash. Any check failure rolls back to the snapshot and reports an error.
5. `CALIB_CANCEL` — roll back at any point and return to `IDLE`.

Out-of-order commands are rejected with a status error instead of being silently accepted, so the
page can always tell whether a step actually ran.

### 4.5 Polling rate — restart required

The rate is baked into the HID **`bInterval`** field of the configuration descriptor
(`bInterval = 6 - level`: 250 Hz → 6, 8 kHz → 1). The descriptor is built during enumeration, so:

> **After changing the polling rate you must send `MSP_REBOOT` (the page's “Reboot device”
> button) — or replug — for the new rate to take effect.**

The rate is never applied at runtime because the host only reads `bInterval` during enumeration.
Deadzone and calibration, by contrast, are applied to the next report immediately.

Notes for real-world 8 kHz:
+ The host must support High-Speed interrupt polling at 125 µs (Linux `usbhid` does; Windows
  behaviour depends on the controller driver).
+ The cable and hub must be genuine High-Speed (480 Mbps). On a Full-Speed port the device still
  works, but the actual rate degrades to 1 ms.
+ The send path is token-driven: a new report is only built when the previous transfer completed,
  so the effective rate is exactly the host's polling rate — never a burst.

### 4.6 Flash storage layout

| Region | Address | Size | Contents |
|---|---|---|---|
| Firmware | `0x08000000` – `0x0803DFFF` | ≤ 248 KB | Code + rodata; a linker `ASSERT` fails the build if the image ever reaches the config sector |
| Config A (primary) | `0x0803E000` | 4 KB | `ConfigHeader{magic 0xCF07, version 1, count}` + TLV records + footer + CRC16 |
| Config B (staging) | `0x0803F000` | 4 KB | Same layout, written first on every save |

Save sequence: build the image in RAM → **byte-compare with sector A and skip the whole erase if
nothing changed** (Flash endurance protection) → erase B, program B, verify B → erase A, program A,
verify A. At every instant one of the two sectors holds a complete, CRC-valid image; boot prefers
A and falls back to B. Records whose length or content fails validation are skipped, leaving the
factory default for that group.

---

## 5. DS4 report layout (for reference)

Input report ID `0x01`, 64 bytes total (63-byte payload as declared in the report descriptor):

| Byte | Content |
|---|---|
| 0 | Report ID `0x01` |
| 1–4 | Left X, Left Y, Right X, Right Y (0–255, centre 128) |
| 5 | D-pad low nibble (0=N, 1=NE … 7=NW, 0x0F=neutral) + □ × ○ △ in bits 4–7 |
| 6 | L1, R1, L2, R2, Share, Options, L3, R3 |
| 7 | PS (bit 0), Touchpad (bit 1), 6-bit rolling report counter (bits 2–7) |
| 8–9 | Left / right trigger (0x00 or 0xFF on this hardware) |
| 10–63 | Vendor area: IMU/touchpad/battery fields. Byte 30 is set to `0x1B` (battery full, USB) |

Feature reports returned for host hand-shaking: `0x02` calibration (36 B), `0x03` definition
(47 B), `0x12` MAC (15 B), `0xA3` version/date (48 B), `0xF2` signing state (15 B), `0xF3` reset
auth (7 B). `0xF1` (signature nonce) is STALLed on purpose — no real authentication is implemented.

---

## 6. Directory structure

```text
App/          main loop, TinyUSB configuration (tusb_config.h)
Bsp/          board init, IRQ vectors (USBHS/TIM2), clock tree, Link.ld
Config/       config registry, TLV + CRC16 storage, Flash HAL
Driver/
  ds4/        DS4 report packing, report descriptor, feature reports
  gamepad/    button/d-pad mapping into GamepadState
  input/      GPIO button scan (with debounce) + ADC stick sampling
  joystick/   calibration state machine, deadzone / full-scale mapping
  msp/        MSP v1 state machine + command dispatch
  cdc/        CDC pipe, feeds bytes into the MSP parser
  usb/        descriptor routing (HID vs CDC), device stack init
  timer/      TIM2 microsecond timebase (micros())
  stat/       optional block-time profiler (compiled out by default)
Core/         RISC-V core support (PFIC/NVIC helpers, CSR accessors)
Debug/        delay routines and printf retarget (USART2)
Startup/      startup_ch32v30x_D8C.S (interrupt vector table, reset handler)
Peripheral/   WCH CH32V30x standard peripheral library
TinyUSB/      TinyUSB 0.21 (git submodule)
obj/          build output (git-ignored)
Docs/         archived design notes — historical, may be outdated
PCB/          hardware releases (v0.1.0, v0.2.0)
```

---

## 7. Attribution & license

This firmware stands on the shoulders of these open projects:

+ **[TinyUSB](https://github.com/hathach/tinyusb)** — Ha Thach (hathach) and contributors.
  The USB device stack, the WCH CH32V307 USBHS port and the HID/CDC class drivers all come from
  TinyUSB 0.21 (MIT).
+ **WCH / 南京沁恒微电子** — CH32V30x standard peripheral library, startup code and the USBHS
  clock/PLL bring-up sequence, taken from the CH32V307 EVT examples.
+ **[GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE)** — reference for the DS4 report
  descriptor, the feature-report hand-shaking data and the “report ID embedded in the payload”
  transmission trick.
+ The DS4/PS4 protocol research community — the report layout and the authentication hand-shake
  behaviour documented by various reverse-engineering projects.
+ **[betaflight](https://github.com/betaflight/betaflight)** — inspiration for the parameter-group
  (TLV + validation callback) configuration design.

Released under the **MIT License** — see [LICENSE](LICENSE).
`Peripheral/`, `Core/`, `Startup/` and `Debug/` are derived from WCH's EVT packages and keep their
original copyright headers.
