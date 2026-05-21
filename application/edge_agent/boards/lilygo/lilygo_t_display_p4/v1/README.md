# Lilygo T-Display-P4 Board Support

## **English | [Chinese](./README_CN.md)**

Upstream board repository:
[T-Display-P4](https://github.com/Xinyuan-LilyGO/T-Display-P4)

## Overview

This board keeps the ESP Board Manager entry points small and explicit. The
custom devices no longer call `TDisplayP4Driver::Init()`, because that routine
brings up the whole board. Power, display, brightness, and ES8311 audio are
exposed as separate Board Manager devices:

| Device name | Purpose |
| --- | --- |
| `power_ctrl` | Initializes and configures XL9535 only, then releases the Board Manager I2C reference. |
| `audio_dac` | Initializes the ES8311 playback path through Board Manager `audio_codec`. |
| `audio_adc` | Initializes the ES8311 capture path through Board Manager `audio_codec`. |
| `display_lcd` | Initializes the auto-detected MIPI LCD panel and exposes the panel handle. |
| `lcd_brightness` | Initializes and controls only the detected screen backlight path. |

Other board interfaces are described in `board_peripherals.yaml` so esp-claw
can bind future devices to Board Manager peripherals without having them hidden
inside a full Lilygo board initialization.

## Directory Layout

| File | Description |
| --- | --- |
| `board_info.yaml` | Board name, chip, manufacturer, and description metadata. |
| `board_devices.yaml` | Board Manager device list and component dependencies. |
| `board_peripherals.yaml` | Reusable I2C, I2S, SPI, and UART interfaces for esp-claw devices. |
| `Kconfig.projbuild` | Camera type and pixel format options. |
| `sdkconfig.defaults.board` | Default configuration for flash, PSRAM, display support, Hosted Wi-Fi, camera, and logs. |
| `setup_device.cpp` | Custom power, display, and brightness device implementation. |

## Quick Start

### Generate Board Manager Files

Run this command from `application/edge_agent`:

```powershell
python managed_components/espressif__esp_board_manager/gen_bmgr_config_codes.py -b ./boards/lilygo/lilygo_t_display_p4/v1 -c ./boards --project-dir .
```

Generated files are written to:

```text
components/gen_bmgr_codes
```

> [!IMPORTANT]
> `managed_components\espressif__esp_board_manager` must exist before running
> this command, otherwise generation will fail.
> On Windows PowerShell, keep `PYTHONIOENCODING` set to `utf-8` if the console
> cannot print the generator's Unicode status icons.

### Required sdkconfig Configuration

Make sure the board defaults are merged into the project configuration. The
generated sdkconfig defaults are written to:

```text
components/gen_bmgr_codes/board_manager.defaults
```

### Board Options

The board-specific Kconfig menu provides these options:

| Option group | Choices |
| --- | --- |
| Camera type | `OV2710`, `SC2336`, `OV5645` |
| Screen pixel format | `RGB565`, `RGB888` |
| Camera pixel format | `RGB565`, `RGB888` |

Default configuration:

- `OV2710`
- screen format `RGB565`
- camera format `RGB565`

If you use another camera or pixel format, adjust the corresponding
Kconfig options through `sdkconfig.defaults.board` or `menuconfig` before
generation/build.
