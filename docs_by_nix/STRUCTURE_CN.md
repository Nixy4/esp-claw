# ESP-Claw 工程代码结构说明

本文档描述 ESP-Claw 固件工程的目录组织与各模块职责。

---

## 顶层目录

```
esp-claw/
├── application/          # 可部署的应用程序
│   ├── edge_agent/       # 主应用：边缘 AI 智能体
│   └── mcp_server_point/ # 独立 MCP 服务端应用
├── components/           # 可复用固件组件
│   ├── claw_modules/     # 核心框架模块
│   ├── claw_capabilities/# Agent 能力（工具）模块
│   ├── lua_modules/      # Lua 硬件驱动与功能模块
│   └── common/           # 公共基础组件
├── docs/                 # 文档站源码（Astro/pnpm）
├── .agents/              # 代码智能体辅助说明
│   ├── design.md         # 架构约束
│   ├── docs.md           # 文档规范
│   ├── gotchas.md        # 常见陷阱
│   └── spec/             # 模块规范（Lua 模块、Skill 规范）
├── AGENTS.md             # 代码智能体总览说明
├── README.md             # 英文项目简介
└── README_CN.md          # 中文项目简介
```

---

## application/edge_agent — 主应用

边缘 AI 智能体的完整固件应用，基于 ESP-IDF 构建。

```
edge_agent/
├── main/
│   ├── main.c            # 应用入口：启动流程、存储挂载、组件初始化
│   ├── app_fs.c/h        # 文件系统初始化（FATFS 分区挂载）
│   ├── Kconfig.projbuild # 应用级 Kconfig 配置项
│   └── idf_component.yml # 组件依赖清单
│
├── boards/               # 开发板适配目录（按厂商组织）
│   ├── espressif/        # 乐鑫官方板（11 款）
│   ├── dfrobot/          # DFRobot（6 款）
│   ├── m5stack/          # M5Stack（3 款）
│   ├── lilygo/           # LilyGo（2 款）
│   ├── movecall/         # Movecall（3 款）
│   ├── waveshare/        # Waveshare（3 款）
│   ├── rockbase-iot/     # Rockbase-IoT（1 款）
│   └── Nologo.Tech/      # Nologo.Tech（1 款）
│
├── components/           # 应用私有组件
│   ├── app_config/       # 应用配置 Schema 与存储
│   ├── cmd_wifi/         # Wi-Fi CLI 命令
│   └── http_server/      # 本地配置 HTTP 服务
│       └── frontend_source/  # 嵌入式配置页前端（Vue.js）
│           ├── src/
│           │   ├── api/        # HTTP 接口封装
│           │   ├── components/ # UI 组件
│           │   ├── pages/      # 页面
│           │   ├── state/      # 状态管理
│           │   └── i18n/       # 多语言
│           └── dist/           # 编译产物（内嵌固件）
│
├── fatfs_image/          # 编译期 FATFS 文件系统内容
│   ├── system/           # 只读 SYSTEM 分区（挂载于 /system）
│   │   └── .recovery/    # 恢复默认配置文件
│   └── storage/          # 可写 DATA 分区种子内容
│       ├── skills/       # 内置 Skill 定义
│       │   ├── light_switch/     # 灯控示例 Skill
│       │   ├── plan_mode/        # 计划模式 Skill
│       │   └── scheduled_task/   # 定时任务 Skill
│       └── static/       # 静态资源
│
├── partitions_8MB.csv    # 分区表（8 MB Flash）
├── partitions_16MB.csv   # 分区表（16 MB Flash）
├── sdkconfig.defaults    # ESP-IDF 默认配置
└── tools/cmake/          # 自定义 CMake 工具脚本
```

### 开发板目录结构

每个开发板目录通常包含：

```
boards/<厂商>/<板型>/
├── board.yaml            # 板型元数据（芯片、外设声明）
├── board_setup.c         # 板级初始化代码
├── board_defaults.json   # 板级默认配置
├── sdkconfig.board       # 板型专属 sdkconfig 片段
├── fatfs_image/          # 板级 SYSTEM 分区覆盖（可选）
└── components/           # 板型私有组件（可选，如屏幕驱动）
```

---

## application/mcp_server_point — MCP 服务端应用

独立的 MCP（Model Context Protocol）服务端固件，目前仅支持 ESP Ditto 开发板，可被其他设备或云端作为 MCP Client 接入。

```
mcp_server_point/
├── main/main.c                   # MCP 服务端入口
├── boards/espressif/esp_Ditto/   # 仅支持 ESP Ditto
└── components/
    └── mcp_server_point_tools/   # MCP 服务端工具集
```

---

## components/claw_modules — 核心框架模块

Agent Runtime 的核心逻辑层，各模块职责明确、边界清晰。

| 模块 | 职责 |
|------|------|
| `claw_core` | Agent 核心循环：上下文构建 → LLM 调用 → 工具执行 → 持久化 → 响应分发；包含 LLM 后端适配（OpenAI/Anthropic）与媒体推理 |
| `claw_cap` | 能力（Capability）注册与分发的公共层 |
| `claw_event_router` | 事件路由：根据 DATA 根目录下的 `router_rules/router_rules.json` 声明式匹配事件并触发动作 |
| `claw_manager` | Agent 实例管理（创建、生命周期、请求队列） |
| `claw_memory` | 会话历史、档案/长期记忆提供者、记忆持久化、请求门控、阶段笔记 |
| `claw_skill` | Skill 注册表：扫描 SYSTEM 与 DATA 两路径下的 Skill，DATA 优先 |
| `claw_ramfs` | RAM 文件系统支持 |
| `claw_utils` | 通用工具函数库 |

```
claw_modules/
├── claw_core/
│   ├── include/
│   ├── src/
│   │   └── llm/
│   │       ├── backends/     # OpenAI / Anthropic 后端实现
│   │       └── media/        # 媒体（图像等）推理支持
│   └── test_apps/            # 组件测试应用
├── claw_manager/
│   └── test_apps/
├── claw_memory/
│   └── skills/               # memory_ops / profile_memory_ops 内置 Skill
└── ...
```

---

## components/claw_capabilities — Agent 能力模块

Agent 可调用的工具（Capabilities），通过 `components/common/app_claw/app_capabilities.c` 统一注册。每个能力模块自管配置、凭证与存储路径。

| 模块 | 功能 |
|------|------|
| `cap_lua` | 执行 Lua 脚本（动态加载设备行为） |
| `cap_files` | 文件系统读写操作 |
| `cap_http_request` | 发起 HTTP 请求 |
| `cap_web_search` | 联网搜索 |
| `cap_mcp_client` | 作为 MCP Client 连接外部 MCP 设备 |
| `cap_mcp_server` | 作为 MCP Server 对外暴露能力 |
| `cap_im_platform` | IM 平台收发消息（Telegram / QQ / 飞书 / 微信） |
| `cap_im_local` | 本地 IM 通道（串口/Web 等） |
| `cap_llm_config` | LLM 后端配置管理 |
| `cap_llm_inspect` | LLM 信息查询与图像理解 |
| `cap_skill_mgr` | Skill 的启用/禁用/查询管理 |
| `cap_router_mgr` | 事件路由规则的动态管理 |
| `cap_scheduler` | 定时/周期任务调度 |
| `cap_session_mgr` | 会话历史管理 |
| `cap_agent_mgr` | Agent 实例管理操作 |
| `cap_boards` | 开发板外设信息查询 |
| `cap_system` | 系统信息、重启等系统级操作 |
| `cap_cli` | 命令行接口能力 |

部分能力模块在其 `skills/` 子目录中附带配套的内置 Skill 文档（如 `cap_http_request`、`cap_router_mgr`、`cap_scheduler` 等）。

---

## components/lua_modules — Lua 模块

通过 `components/common/app_claw/app_lua_modules.c` 统一注册，分为硬件驱动与功能模块两类。

### 硬件驱动（8 个）

| 模块 | 说明 |
|------|------|
| `lua_driver_gpio` | GPIO 控制 |
| `lua_driver_adc` | ADC 模数转换 |
| `lua_driver_i2c` | I2C 通信 |
| `lua_driver_uart` | UART 串口通信 |
| `lua_driver_rmt` | RMT 红外/LED 时序信号 |
| `lua_driver_mcpwm` | MCPWM 电机控制 PWM |
| `lua_driver_pcnt` | PCNT 脉冲计数器 |
| `lua_driver_touch` | 触摸传感器 |

### 功能模块（28 个）

| 模块 | 说明 |
|------|------|
| `lua_module_storage` | 文件存储（封装 DATA 根路径） |
| `lua_module_json` | JSON 序列化/反序列化 |
| `lua_module_thread` | 多线程（FreeRTOS 任务封装） |
| `lua_module_delay` | 延时操作 |
| `lua_module_system` | 系统信息与控制 |
| `lua_module_http_server` | 本地 HTTP 服务器 |
| `lua_module_event_publisher` | 向事件路由发布事件 |
| `lua_module_call_capability` | 在 Lua 中调用 Agent 能力 |
| `lua_module_board_manager` | 读取开发板外设配置 |
| `lua_module_audio` | 音频录放 |
| `lua_module_camera` | 摄像头采集 |
| `lua_module_display` | 通用显示接口 |
| `lua_module_lcd` | LCD 屏幕驱动 |
| `lua_module_lcd_touch` | LCD 触摸屏 |
| `lua_module_lvgl` | LVGL GUI 框架 |
| `lua_module_image` | 图像处理 |
| `lua_module_vision` | 视觉识别 |
| `lua_module_led_strip` | WS2812 等 LED 灯带控制 |
| `lua_module_button` | 按键检测 |
| `lua_module_knob` | 旋钮编码器 |
| `lua_module_ble` | 蓝牙 BLE |
| `lua_module_ble_hid` | BLE HID 设备模拟 |
| `lua_module_ir` | 红外发送/接收 |
| `lua_module_imu` | IMU 惯性测量单元 |
| `lua_module_environmental_sensor` | 温湿度等环境传感器 |
| `lua_module_magnetometer` | 磁力计 |
| `lua_module_fuel_gauge` | 电量计（电池电量） |
| `lua_module_sci` | SCI 串行通信接口 |

---

## components/common — 公共基础组件

| 组件 | 职责 |
|------|------|
| `app_claw` | 应用外壳：能力注册（`app_capabilities.c`）、Lua 模块注册（`app_lua_modules.c`）、CLI、Agent 启动 |
| `wifi_manager` | Wi-Fi 连接管理 |
| `settings` | 设备配置项存储与读取 |
| `skill_builder` | 组件级 Skill 构建辅助（将组件内 `skills/` 同步至 SYSTEM 镜像） |
| `lua_module_builder` | Lua 模块构建辅助 |
| `http_reuse` | HTTP 连接复用 |
| `mcp_mdns` | MCP 设备 mDNS 发现 |
| `display_arbiter` | 多任务显示资源仲裁 |
| `emote` | 表情/状态指示（LED/屏幕） |
| `esp_painter` | 2D 绘图工具 |
| `esp_video` | 视频流处理 |
| `captive_dns` | Captive Portal DNS（Wi-Fi 配网门户） |

---

## 文件系统路径约定

运行时有两个逻辑根路径，通过 `claw_paths` 在 C 层、`storage` 模块在 Lua 层解析，**禁止在可复用代码中硬编码 `/fatfs`**。

| 路径常量 | 挂载点 | 特性 | 内容 |
|----------|--------|------|------|
| `CLAW_PATH_SYSTEM` | `/system` | 只读 | 内置 Skill、内置 Lua 脚本/文档、板级 FATFS 覆盖、恢复种子 |
| `CLAW_PATH_DATA` | `/fatfs` 或 SD 卡挂载点 | 可写 | 用户 Skill、路由规则、调度规则、记忆、会话、收件箱、用户文件 |

- 组件内置 Skill 脚本路径写法：`{CUR_SKILL_DIR}/scripts/...`（由 Skill 运行时展开）
- DATA 路径在 C 中：`claw_paths_join(CLAW_PATH_DATA, "...")`
- DATA 路径在 Lua 中：`storage.join_path(storage.get_root_dir(), "...")`

---

## 数据流总览

```
IM 频道 / 定时调度 / Lua 脚本 / CLI
          │  发布事件 / 提交请求
          ▼
   claw_event_router          ← router_rules/router_rules.json
   （事件匹配 → 动作分发）
          │
    ┌─────┴──────────────────────────────┐
    │ 调用能力 / 运行脚本 / 触发 Agent  │
    └──────────────┬─────────────────────┘
                   ▼
              claw_core
     ┌─────────────────────────┐
     │ 构建上下文（记忆/历史/Skill）│
     │ → 调用 LLM 后端          │
     │ → 执行工具调用（Capability）│
     │ → 持久化上下文            │
     │ → 分发响应               │
     └─────────────────────────┘
                   │
          IM 绑定 / 本地通道 / Web
```

---

## 开发命令速查

```bash
# 导出 ESP-IDF 环境（每次新终端需执行）
. $IDF_PATH/export.sh

# 生成开发板配置并编译
cd application/edge_agent
idf.py bmgr -c ./boards -b esp32_S3_DevKitC_1
idf.py build

# 烧录并监视串口
idf.py flash monitor

# 编译嵌入式前端
cd application/edge_agent/components/http_server/frontend_source
pnpm build
pnpm typecheck

# 本地运行文档站
cd docs
pnpm install && pnpm dev
```

---

## 扩展点指引

| 扩展目标 | 推荐路径 |
|----------|----------|
| 新增 Agent 可调用工具 | `components/claw_capabilities/` 新建能力模块，并在 `app_capabilities.c` 注册 |
| 新增 Lua 硬件驱动/功能模块 | `components/lua_modules/` 新建模块，并在 `app_lua_modules.c` 注册 |
| 适配新开发板 | `application/edge_agent/boards/<厂商>/<板型>/` 新建目录，添加 `board.yaml` 与初始化代码 |
| 添加内置 Skill | 在对应能力或模块组件目录下新建 `skills/<skill_id>/SKILL.md`，由 `skill_builder` 同步至 SYSTEM |
| 修改默认路由规则 | `application/edge_agent/fatfs_image/storage/` 下的恢复种子，或运行时通过 `cap_router_mgr` 操作 |
| 添加内置 DATA 种子内容 | `application/edge_agent/fatfs_image/storage/` |
