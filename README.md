# CH32V307 DS4 8K Gamepad

> A complete PS4 DualShock 4 gamepad solution with an 8000 Hz polling rate: firmware + PCB + web configurator

`v0.1.0` · Web configurator: [P1nkDog/ds4-gamepad-configurator](https://github.com/P1nkDog/ds4-gamepad-configurator)

English | [中文](./README.zh-CN.md)

---

## Features

- **8 kHz polling rate** — uses the CH32V307 USB High-Speed port (480 Mbps); with HID endpoint `bInterval = 1` that is 125 µs per report. Six selectable rates (250 / 500 / 1k / 2k / 4k / 8k), default 1 kHz
- **Native DS4 device** — the report descriptor and the 64-byte input report follow the DualShock 4 spec, so the host recognises it as a PS4 controller with no driver or remapping software
- **HID / CDC dual mode** — at power-up the firmware reads the Share button (PB12) and boots either as a gamepad or as a serial configuration device
- **MSP v1 configuration protocol** — 15 commands covering version queries, config read/write, live stick values, step-by-step calibration and device reboot
- **Config stored in Flash** — two sectors written alternately (`0x08046000` / `0x08047000`) with magic `0xCF07` and CRC16, so a corrupted write falls back to the other sector
- **Stick processing** — per-axis calibration (min / center / max), ADC inversion, deadzone, and full-scale mapping outside the deadzone
- **Configured from the browser** — deadzone, per-axis calibration and polling rate are all editable in a web page over Web Serial, no driver needed

> 8 kHz requires both the host and the cable to support USB High-Speed; the real rate also depends on host scheduling and degrades gracefully when plugged into a Full-Speed port.

## Hardware

### MCU and key components

| Item | Detail |
|---|---|
| MCU | CH32V307VCT6 (RISC-V RV32IMACXW, 288K Flash / 32K RAM, on-chip USBHS PHY) |
| USB | USB 2.0 High-Speed (480 Mbps), device descriptor `bcdUSB = 0x0200` |
| Sticks | Two analog sticks, sampled through ADC1 |
| Buttons | Tactile switches plus one BOOT0 slide switch |
| On-board LED / extra button | Reserved in code (PA0 for LED, active low; PA2 for a button) — **not routed on the current board**, see `Bsp/board.h` |

### Pin assignment

**Sticks (ADC1)**

| Axis | Pin | ADC channel |
|---|---|---|
| Left stick X | PC4 | ADC14 |
| Left stick Y | PC5 | ADC15 |
| Right stick X | PA7 | ADC7 |
| Right stick Y | PA6 | ADC6 |

**Buttons (active low, internal pull-up)**

| Function | Pin | Function | Pin |
|---|---|---|---|
| D-pad up | PE9 | L1 | PE7 |
| D-pad down | PE11 | R1 | PB10 |
| D-pad left | PE10 | L2 | PB0 |
| D-pad right | PE8 | R2 | PB1 |
| △ / Y | PE15 | Left stick click (L3) | PA5 |
| ○ / B | PE14 | Right stick click (R3) | PE1 |
| × / A | PE13 | PS | PE12 |
| □ / X | PB11 | Touchpad (SW3) | PB13 |
| Options / Start (SW4) | PB14 | Share / Select (SW2) | PB12 |

**Extra buttons (scanned, not reported to DS4)**

| No. | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Pin | PD3 | PD4 | PD5 | PD6 | PD7 | PB3 | PB4 | PE0 |

**Switches**

| Part | Function | Behaviour |
|---|---|---|
| SW5 | BOOT0 | Set to **1** → 3.3 V, the chip enters its Bootloader on power-up (for flashing); back to **0** → GND, normal firmware boot |
| SW2 | Share / Select | **Hold while powering up** (pulls PB12 low) → boots in CDC configuration mode; releasing it → HID gamepad mode |

### PCB

Hardware files live in per-version folders; the current one is [`PCB/v0.1.0/`](./PCB/v0.1.0/) and covers three boards: **main board**, **Type-C daughter board** and **panel**.

| File | Contents |
|---|---|
| `8khz-ds4-gamepad-ch32v307.epro2` | EDA project source, import it to keep editing |
| `8khz-ds4-gamepad-ch32v307.zip` | Two schematic PDFs: `SCH_Schematic1_1-主板`, `SCH_Schematic2_1-小板` |
| `main board.zip` | Main board Gerber (upload this one to order fabrication) |
| `type-c board.zip` | Type-C daughter board Gerber |
| `panel.zip` | Panelised Gerber — for fabricating several boards in one panel |

All three Gerber packages are identical in structure, 16 files each: copper (GTL/GBL), silkscreen (GTO/GBO), solder mask (GTS/GBS), paste (GTP/GBP), board outline (GKO), drill drawing (GDL/GDD), PTH/NPTH/via drill files (DRL) and a flying-probe test file, plus a `PCB下单必读.txt` note — **upload the whole package to a PCB house and it is ready to order**.

**Note that the v0.1.0 PCB uses the DS5 version of the analog stick module, and the board thickness should be 1.2 mm.**

> BOM and pick-and-place files are not included; export them separately if you want full SMT assembly.

## Quick start

### Prerequisites

- **MounRiver Studio (MRS) 2.5.0** or newer, which bundles `riscv-none-embed-gcc`
- Clone the sources (TinyUSB is a submodule, so `--recursive` is required):

  ```bash
  git clone --recursive https://github.com/P1nkDog/8khz-ds4-gamepad-ch32v307.git
  ```

- For flashing, either a **WCH-Link** (download straight from MRS) or **any USB cable plus the web flasher**

### Build

1. MRS → `File` → `Import` → `Existing MounRiver Studio Project`, then pick this folder
2. `Project` → `Build Project` (Ctrl+B)
3. Output lands in `obj/`: `8khz-ds4-gamepad-ch32v307.hex` / `.elf` / `.map`

   > The file name is the MRS project name, taken from the `<name>` field in `.project` (`.cproject` uses `artifactName="${ProjName}"`) — **it does not depend on what the cloned folder is called**. This repo's project name is already `8khz-ds4-gamepad-ch32v307`; rename the project in MRS and the artifact name follows.

There is no `Makefile` at the repo root: `obj/` (with `makefile`, `.o` and `.hex` inside) is generated by MRS on the first Build and is listed in `.gitignore` — **a fresh clone having no `obj/` is expected**. Building from a shell means pointing the toolchain at it yourself; using MRS is the normal path.

### Flash

**Option 1: web flasher (recommended, no tools to install, Windows)**

1. Set **BOOT0** (under the board) to **1**
2. Connect the board with a USB cable
3. Use [Zadig](https://zadig.akeo.ie/) to bind the **WinUSB** driver to the device with `USB ID 4348 55E0` (once only)
4. Open the **Flash** page of the [web configurator](https://github.com/P1nkDog/ds4-gamepad-configurator) → click **[Connect]** in the top right → pick "Unknown Device from WinChipHead"
5. Choose `obj/8khz-ds4-gamepad-ch32v307.hex`, or a version listed under **Online flash** → **[Flash]**
6. **Set BOOT0 back to 0 and power-cycle the board**

**Option 2: MRS + WCH-Link**

Hook up the WCH-Link and click `Download` in MRS (BOOT0 can stay at 0 — the WCH-Link goes through the debug port).

**Option 3: WCHISPTool**

The official Windows flashing tool: choose the USB transport → select `obj/8khz-ds4-gamepad-ch32v307.hex` → download. BOOT0 must be set to 1 first, same as above.

## Project structure

```
8khz-ds4-gamepad-ch32v307/
├── App/
│   ├── main.c                  # main loop: tud_task + HID/CDC branch
│   └── tusb_config.h           # TinyUSB config (HID + CDC, endpoint buffers)
├── Bsp/
│   ├── board_init.c/.h         # clocks, USBHS, LED, button, SysTick
│   ├── Link.ld                 # linker script: 288K Flash + 32K RAM
│   ├── ch32v30x_it.c           # interrupt handlers
│   ├── system_ch32v30x.c       # system clock
│   └── ch32v30x_conf.h         # peripheral library config
├── Config/
│   ├── config.c/.h             # config group registry, TLV serialisation, dual-sector switch
│   ├── config_crc.h            # CRC16
│   └── config_hal.c/.h         # Flash sector read/write (0x08046000 / 0x08047000)
├── Core/                       # RISC-V core support (CSR / PFIC / SysTick)
├── Driver/
│   ├── ds4/                    # DS4 report descriptor + 64-byte input report
│   ├── cdc/                    # CDC virtual serial port (configuration mode)
│   ├── usb/                    # descriptor routing + HID/CDC mode selection
│   ├── msp/                    # MSP v1 framing and command handling
│   ├── input/                  # ADC stick sampling + button scanning
│   ├── joystick/               # inversion, deadzone, full-scale mapping, calibration state machine
│   ├── gamepad/                # gamepad state assembly (buttons → DS4 fields)
│   └── timer/                  # TIM2 microsecond timer
├── Peripheral/                 # CH32V30x standard peripheral library (28 modules)
├── Startup/                    # startup code (D8C variant)
├── TinyUSB/                    # TinyUSB submodule v0.21.0
├── PCB/v0.1.0/                 # hardware project (EDA source / schematic PDFs / Gerber)
├── obj/                        # build output (gitignored)
├── README.md                   # English readme (this file)
├── README.zh-CN.md             # Chinese readme
└── LICENSE                     # GPL-3.0
```

## Firmware architecture

```
App/main.c ── main loop (tud_task() polled continuously)
│
├─ HID mode (power up without holding Share)
│    gamepad_process()
│      ├─ input_read_buttons()   button scan (GPIO)
│      ├─ joystick_process()     ADC → invert → deadzone → full-scale mapping
│      └─ ds4_process()          build the 64-byte report
│    → TinyUSB HID interrupt endpoint (bInterval=1 → 125 µs)
│
└─ CDC mode (power up holding Share)
     cdc_process()
       └─ msp_handler()          MSP v1 command parsing
            ├─ CONFIG_GET/SET/SAVE/RESET   → Config
            ├─ CALIB_START/SAMPLE/TRACK/END → joystick
            ├─ JOYSTICK_RAW                 → raw stick values
            └─ REBOOT                       → software reset
     → TinyUSB CDC → browser Web Serial

Config ── three config groups + TLV serialisation + CRC16 + alternating sectors (0x08046000 / 0x08047000)
```

**Boot order**: `board_init()` → `config_init()` (read Flash, verify, fall back to defaults) → `us_timer_init()` → `usb_desc_init()` (reads PB12 to pick the mode) → `tud_init()` → `gamepad_init()`.

**Fixed-rate reporting**: the main loop uses the TIM2 microsecond counter and only runs `gamepad_process()` + `ds4_process()` when `micros() - last >= configured interval`. The interval comes from a lookup table indexed by the `polling_rate` config group (8k → 125 µs).

**Modes are mutually exclusive**: `gamepad_process()` does not run in CDC mode, so no gamepad data is reported at all — that is a consequence of a USB device enumerating one configuration at a time, not a defect.

## Web configurator

[P1nkDog/ds4-gamepad-configurator](https://github.com/P1nkDog/ds4-gamepad-configurator) — a pure front-end page with two independent channels:

| Purpose | Channel | Device state |
|---|---|---|
| Change settings, calibrate sticks | Web Serial (CDC) | Firmware running, CDC mode |
| Flash firmware | WebUSB (WCH ISP, `4348:55E0`) | Firmware not running, chip in Bootloader |

Configuration flow: hold **Share** while plugging in (or press **RST**) to enter CDC mode → open the page in the browser → click **[Connect]** → change settings → **[Save]** in the bottom bar (after changing the polling rate also click **[Reboot device]**).

## Configuration

### Config groups

| ID | Name | Struct | Version | Default | Notes |
|---|---|---|---|---|---|
| 1 | `joystick_calib` | `JoystickCalibConfig` | 1 | all axes `min=0 / center=2048 / max=4095` | per-axis calibration |
| 2 | `gamepad_settings` | `GamepadSettings` | 1 | `deadzone = 1` (%) | the deadzone is **shared by the whole gamepad** |
| 3 | `polling_rate` | `PollingRateConfig` | 1 | `POLL_RATE_1000` | six levels: 250 / 500 / 1k / 2k / 4k / 8k |

Storage is TLV: a header (magic `0xCF07` + version + count) → a number of records (type / length / version / flags / data) → a terminator. On read, records whose version does not match are ignored and the defaults are used, so bump the version whenever the struct changes.

### Stick processing

1. **ADC sampling** → raw 12-bit value
2. **Hardware inversion** (some potentiometers are wired the other way, normalised here)
3. **Deadzone**: relative to `center`, anything inside the deadzone outputs the midpoint `2048`
4. **Full-scale mapping outside the deadzone**: `[center ± dz, min/max]` is stretched linearly to `[0, 2048]` / `[2048, 4095]`, so pushing to the limit really reaches full scale
5. **Four-step calibration** (matching `CALIB_START / SAMPLE / TRACK / END`): sample the centre (16-sample average) → track each axis's extremes → write back to config and save

### TinyUSB configuration

Key entries in `App/tusb_config.h`:

| Macro | Value | Purpose |
|---|---|---|
| `CFG_TUD_HID` / `CFG_TUD_CDC` | 1 / 1 | both device classes compiled in, chosen by mode at runtime |
| `CFG_TUD_ENDPOINT0_SIZE` | 64 | control endpoint |
| `CFG_TUD_HID_EP_BUFSIZE` | 64 | matches the DS4 report length |
| `CFG_TUD_CDC_*_BUFSIZE` | 512 high-speed / 64 full-speed | serial buffers |
| `CFG_TUSB_OS` | `OPT_OS_NONE` | bare metal (no RTOS); `tud_task()` is polled from the main loop |

### Button mapping

Physical button → DS4 field, hard-coded in `Driver/gamepad/gamepad.c`:

| Button | DS4 field | Button | DS4 field |
|---|---|---|---|
| □ / X (PB11) | `buttonWest` | L1 (PE7) | `buttonL1` |
| × / A (PE13) | `buttonSouth` | R1 (PB10) | `buttonR1` |
| ○ / B (PE14) | `buttonEast` | L2 (PB0) | `buttonL2` |
| △ / Y (PE15) | `buttonNorth` | R2 (PB1) | `buttonR2` |
| Share (PB12) | `buttonSelect` | L3 (PA5) | `buttonL3` |
| Options (PB14) | `buttonStart` | R3 (PE1) | `buttonR3` |
| PS (PE12) | `buttonHome` | D-pad | `dpad` (8 directions) |
| Touchpad (PB13) | `buttonTouchpad` | | |

## Status

**Implemented**

- DS4 HID reporting (64 bytes: sticks, buttons, D-pad, PS / Touchpad)
- Fixed-rate reporting up to 8 kHz (USB High-Speed), six selectable levels
- CDC configuration mode + MSP v1 (15 commands)
- Dual-sector config storage with CRC and power-loss fallback
- Per-axis calibration and deadzone mapping
- Browser-side settings, calibration and flashing

**Not implemented / known limitations**

- Extra buttons BTN1–BTN8 are scanned but never reported (the DS4 input report has no field for them)
- Triggers are **digital** switches with no analog travel, so there are no trigger curves
- Button mapping is hard-coded; no macros, rapid fire or remapping
- The deadzone is a single global value; left and right sticks cannot be set separately
- Configuration mode and gamepad mode are mutually exclusive; switching requires re-plugging
- No gyroscope / accelerometer; Touchpad only reports the click, no coordinates

## References

| Source | Used for |
|---|---|
| [TinyUSB](https://github.com/hathach/tinyusb) | USB stack (including the WCH CH32 USBHS driver) |
| [wchisp-web](https://github.com/P1nkDog/wchisp-web) | Web-side WCH ISP flashing implementation (belongs to the configurator repo, not this firmware) |
| [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) | Gamepad feature set and button layout reference |
| [Betaflight](https://github.com/betaflight/betaflight) | Config group / MSP protocol design reference |
| DualShock 4 report descriptor | Based on a descriptor captured from real hardware |

## License

[GPL-3.0-only](./LICENSE). Free to use, modify and redistribute, but derivative works must also be released under GPL-3.0, with no warranty.

## Acknowledgements

- [TinyUSB](https://github.com/hathach/tinyusb) — the USB stack; device classes, endpoint management and the WCH port layer all come from it
- WCH (沁恒微电子) — the [CH32V307 chip, the standard peripheral library and official examples](https://www.wch.cn/products/CH32V307.html)
- [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE), [Betaflight](https://github.com/betaflight/betaflight) — sources of inspiration for the button layout and configuration system
