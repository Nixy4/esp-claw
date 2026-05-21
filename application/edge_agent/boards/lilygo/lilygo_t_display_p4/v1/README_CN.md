# Lilygo T-Display-P4 板级支持说明

## **[English](./README.md) | 中文**

上游板级仓库：
[T-Display-P4](https://github.com/Xinyuan-LilyGO/T-Display-P4)

## 概览

当前板级适配把 ESP Board Manager 的入口拆小，不再调用
`TDisplayP4Driver::Init()`。这个接口会初始化整块板子的所有外设，不符合
esp-claw 现在希望按设备拆分的方式。

现在电源、屏幕、背光和 ES8311 音频都拆成独立的 Board Manager 设备：

| 设备名 | 作用 |
| --- | --- |
| `power_ctrl` | 只初始化并配置 XL9535，配置完成后立即释放 Board Manager I2C 引用。 |
| `audio_dac` | 通过 Board Manager `audio_codec` 初始化 ES8311 播放路径。 |
| `audio_adc` | 通过 Board Manager `audio_codec` 初始化 ES8311 录音路径。 |
| `display_lcd` | 只初始化自动识别到的 MIPI LCD，并暴露 panel handle。 |
| `lcd_brightness` | 只初始化和控制当前屏幕对应的背光路径。 |

其他板载接口写在 `board_peripherals.yaml` 中，后续 esp-claw 设备可以通过
Board Manager 绑定这些外设接口，而不是被隐藏在 Lilygo 的整板初始化流程里。

## 目录结构

| 文件 | 说明 |
| --- | --- |
| `board_info.yaml` | 板名、芯片、厂商和描述信息。 |
| `board_devices.yaml` | Board Manager 设备列表和组件依赖。 |
| `board_peripherals.yaml` | 给 esp-claw 设备复用的 I2C、I2S、SPI、UART 接口。 |
| `Kconfig.projbuild` | 摄像头类型和像素格式选项。 |
| `sdkconfig.defaults.board` | Flash、PSRAM、屏幕、Hosted Wi-Fi、摄像头和日志默认配置。 |
| `setup_device.cpp` | 自定义电源、屏幕和背光设备实现。 |

## 快速开始

### 生成 Board Manager 文件

进入 `application/edge_agent` 后运行：

```powershell
python managed_components/espressif__esp_board_manager/gen_bmgr_config_codes.py -b ./boards/lilygo/lilygo_t_display_p4/v1 -c ./boards --project-dir .
```

生成结果会输出到：

```text
components/gen_bmgr_codes
```

> [!IMPORTANT]
> 命令执行前需要存在 `managed_components\espressif__esp_board_manager`，
> 否则会生成失败。
> Windows PowerShell 如果无法输出生成脚本里的 Unicode 状态图标，请保留
> `PYTHONIOENCODING=utf-8`。

### 必要 sdkconfig 配置

请确认板子默认配置已经合并到工程配置中。生成的 sdkconfig 默认文件路径为：

```text
components/gen_bmgr_codes/board_manager.defaults
```

### 板子选项

本板子的 Kconfig 菜单提供以下选项：

| 选项组 | 可选项 |
| --- | --- |
| 摄像头类型 | `OV2710`、`SC2336`、`OV5645` |
| 屏幕像素格式 | `RGB565`、`RGB888` |
| 摄像头像素格式 | `RGB565`、`RGB888` |

默认配置为：

- `OV2710`
- 屏幕格式 `RGB565`
- 摄像头格式 `RGB565`

如果使用其他摄像头或像素格式，请在生成或编译前通过
`sdkconfig.defaults.board` 或 `menuconfig` 调整对应 Kconfig 选项。
