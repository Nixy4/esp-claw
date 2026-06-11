# ESP-Claw 后端移植与前端适配指南

本文档面向希望将 ESP-Claw Agent 框架移植到其他 ESP-IDF 项目、同时为其接入新配置前端的开发者。

---

## 目录

1. [架构总览与移植思路](#1-架构总览与移植思路)
2. [后端移植步骤](#2-后端移植步骤)
   - 2.1 依赖组件引入
   - 2.2 文件系统初始化
   - 2.3 配置子系统实现
   - 2.4 启动 Agent（app_claw）
   - 2.5 HTTP 服务与前端挂载
   - 2.6 可选能力裁剪
3. [HTTP API 参考](#3-http-api-参考)
4. [前端适配方案](#4-前端适配方案)
   - 4.1 直接复用现有前端
   - 4.2 替换前端（自定义 UI）
   - 4.3 前后端 API 契约
5. [最小移植模板](#5-最小移植模板)
6. [常见问题](#6-常见问题)

---

## 1. 架构总览与移植思路

### 核心分层

```
┌──────────────────────────────────────────────────────┐
│  新项目 main.c（你的入口）                           │
│    ├── 调用 app_claw_start()  → Agent 核心           │
│    ├── 调用 http_server_init/start() → 配置 HTTP 服务│
│    └── 调用 wifi_manager_*() → 网络管理              │
└──────────────────────────────────────────────────────┘
              ↓ 依赖
┌──────────────────────────────────────────────────────┐
│  components/common/app_claw/   （应用外壳）          │
│    ├── app_capabilities.c  注册所有 Capability       │
│    ├── app_lua_modules.c   注册所有 Lua 模块         │
│    └── app_claw.c          Agent 生命周期管理        │
└──────────────────────────────────────────────────────┘
              ↓ 依赖
┌──────────────────────────────────────────────────────┐
│  components/claw_modules/  （框架核心，不可拆分）    │
│    ├── claw_core     LLM 调用 + 工具执行主循环       │
│    ├── claw_cap      Capability 注册/分发            │
│    ├── claw_event_router  事件路由                   │
│    ├── claw_memory   记忆与会话历史                  │
│    └── claw_skill    Skill 注册表                    │
└──────────────────────────────────────────────────────┘
              ↓ 依赖
┌──────────────────────────────────────────────────────┐
│  components/claw_capabilities/  （Agent 工具）       │
│  components/lua_modules/        （Lua 硬件驱动）     │
└──────────────────────────────────────────────────────┘
```

### 移植的本质

ESP-Claw 的"后端"主要由以下三组可复用组件构成，**全部放在 `components/` 下**，与特定应用无耦合：

| 组件目录 | 性质 | 能否裁剪 |
|----------|------|---------|
| `claw_modules/` | 框架核心 | 不可裁剪（整体依赖） |
| `claw_capabilities/` | Agent 工具集 | 可按需开关（Kconfig） |
| `lua_modules/` | Lua 硬件驱动 | 可按需开关（Kconfig） |
| `common/app_claw/` | 注册外壳 | 直接复用或按需修改 |
| `common/wifi_manager/` | Wi-Fi 管理 | 可替换为自有实现 |

移植策略：**将 `components/` 目录整体复制（或以 ESP-IDF 组件方式引用）到新项目**，然后参照 `application/edge_agent/main/main.c` 编写适合自己项目的入口代码。

---

## 2. 后端移植步骤

### 2.1 依赖组件引入

**方式一：直接复制（推荐快速验证）**

```bash
# 在新项目根目录执行
cp -r <esp-claw>/components ./components
```

**方式二：ESP-IDF 本地组件引用**

在新项目 `CMakeLists.txt` 中追加 `EXTRA_COMPONENT_DIRS`：

```cmake
cmake_minimum_required(VERSION 3.16)
list(APPEND EXTRA_COMPONENT_DIRS
    "<esp-claw路径>/components/claw_modules"
    "<esp-claw路径>/components/claw_capabilities"
    "<esp-claw路径>/components/lua_modules"
    "<esp-claw路径>/components/common"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_project)
```

**方式三：通过 IDF Component Manager 引用（如已发布到组件注册表）**

在项目的 `idf_component.yml` 中声明依赖。

---

### 2.2 文件系统初始化

ESP-Claw 依赖两个 FATFS 分区：

| 分区 | 挂载点 | 说明 |
|------|--------|------|
| SYSTEM（只读） | `/system` | 内置 Skill、Lua 脚本、恢复种子 |
| DATA（可写） | `/fatfs` 或 SD 卡挂载点 | 用户数据、规则、会话、记忆 |

在新项目的文件系统初始化代码中，挂载完成后必须注册这两个路径：

```c
#include "claw_paths.h"

// 挂载文件系统（示例：flash FATFS）
// ... your_fs_mount_code() ...

// 注册路径（必须在 app_claw_start 之前调用）
ESP_ERROR_CHECK(claw_paths_set(CLAW_PATH_DATA, "/fatfs"));
ESP_ERROR_CHECK(claw_paths_set(CLAW_PATH_SYSTEM, "/system"));
```

> **注意**：`/fatfs` 只是 edge_agent 应用的默认值。如果使用 SD 卡，DATA 挂载点会不同，通过 `claw_paths_set` 传入实际路径即可，代码层不需要做任何改动。

参考实现：`application/edge_agent/main/app_fs.c`（初始化逻辑）和 `main.c`（路径注册调用）。

---

### 2.3 配置子系统实现

`app_claw` 通过 `app_claw_config_t` 结构体接收所有运行时配置（LLM 密钥、后端类型、IM 平台凭证等），与持久化存储解耦。

新项目需要实现以下适配：

#### a) 定义配置存储方式

可复用 edge_agent 的 `app_config` 组件（NVS 存储），也可以用自有持久化方案。关键是在启动前填充 `app_claw_config_t`：

```c
#include "app_claw.h"

app_claw_config_t claw_cfg = {0};

// 从你的存储中读取，填充必要字段
strlcpy(claw_cfg.llm_api_key,      my_cfg.llm_api_key,      sizeof(claw_cfg.llm_api_key));
strlcpy(claw_cfg.llm_backend_type, my_cfg.llm_backend_type, sizeof(claw_cfg.llm_backend_type));
strlcpy(claw_cfg.llm_model,        my_cfg.llm_model,        sizeof(claw_cfg.llm_model));
strlcpy(claw_cfg.llm_base_url,     my_cfg.llm_base_url,     sizeof(claw_cfg.llm_base_url));
// ... 其他字段按需填写
```

`app_claw_config_t` 完整字段定义见：
`components/common/app_claw/include/app_claw.h`

#### b) 实现配置保存回调

Agent 核心在运行时可能更新配置（如 LLM 模型切换），需要注册保存回调：

```c
static esp_err_t my_save_claw_config(const app_claw_config_t *config, void *user_ctx)
{
    // 将 config 中的字段写入你的持久化存储
    return my_config_save(config);
}

// 在 app_claw_start 之前注册
ESP_ERROR_CHECK(app_claw_set_save_config_callback(my_save_claw_config, NULL));
```

---

### 2.4 启动 Agent（app_claw）

完成上述准备后，按以下顺序调用：

```c
#include "app_claw.h"
#include "claw_paths.h"

void app_main(void)
{
    // 1. NVS 初始化
    nvs_flash_init();

    // 2. 文件系统挂载 + 路径注册
    my_fs_init();
    claw_paths_set(CLAW_PATH_DATA,   "/fatfs");
    claw_paths_set(CLAW_PATH_SYSTEM, "/system");

    // 3. 可选：UI 初始化（屏幕表情等）
    app_claw_ui_start();

    // 4. 网络初始化（Wi-Fi 或有线）
    my_network_init();

    // 5. 填充配置
    app_claw_config_t claw_cfg = {0};
    my_load_claw_config(&claw_cfg);

    // 6. 注册配置保存回调
    app_claw_set_save_config_callback(my_save_claw_config, NULL);

    // 7. 启动 Agent
    app_claw_start(&claw_cfg);
}
```

---

### 2.5 HTTP 服务与前端挂载

如果新项目需要保留 Web 配置页，可直接复用 edge_agent 的 `http_server` 组件。

#### 复制组件

```bash
cp -r <esp-claw>/application/edge_agent/components/http_server \
      <new_project>/components/http_server
```

#### 初始化调用

`http_server` 通过服务回调与应用解耦，只需实现几个函数指针：

```c
#include "http_server.h"

// 实现所需服务
static esp_err_t my_load_config(app_config_t *config) { ... }
static esp_err_t my_save_config(const app_config_t *config) { ... }
static esp_err_t my_get_wifi_status(http_server_wifi_status_t *status) { ... }
static esp_err_t my_restart(void) { esp_restart(); return ESP_OK; }

// 初始化 HTTP 服务器
http_server_init(&(http_server_config_t){
    .storage_base_path = "/fatfs",
    .services = {
        .load_config      = my_load_config,
        .save_config      = my_save_config,
        .get_wifi_status  = my_get_wifi_status,
        .restart_device   = my_restart,
        // WeChat 登录相关字段按需填写，不需要则留 NULL
    },
});
http_server_start();
```

> `app_config_t` 与 `app_claw_config_t` 的字段对应关系见 `app_config.h` 中的 `app_config_to_claw()` 函数。如果新项目使用自定义配置结构体，需要在 `http_server_config_api.c` 中调整字段映射（或换用自定义 API 处理器）。

---

### 2.6 可选能力裁剪

通过 Kconfig（`sdkconfig`）可按需关闭不需要的能力，减小固件体积：

```kconfig
# 关闭不用的 IM 平台
CONFIG_APP_CLAW_CAP_IM_WECHAT=n
CONFIG_APP_CLAW_CAP_IM_QQ=n
CONFIG_APP_CLAW_CAP_IM_FEISHU=n

# 关闭联网搜索（无网络应用）
CONFIG_APP_CLAW_CAP_WEB_SEARCH=n

# 关闭 MCP Server（不作为 MCP 服务端）
CONFIG_APP_CLAW_CAP_MCP_SERVER=n

# 关闭不使用的 Lua 硬件模块
CONFIG_APP_CLAW_LUA_MODULE_CAMERA=n
CONFIG_APP_CLAW_LUA_MODULE_AUDIO=n
```

建议先在 `sdkconfig.defaults` 中保持开启状态完成验证，再逐步裁剪。

---

## 3. HTTP API 参考

HTTP 服务器暴露以下 REST API，**前端与后端通过这些接口通信**。所有接口均以 `/api/` 为前缀，静态文件和文件系统内容走独立路径。

### 配置接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/config?groups=wifi,llm,...` | 按分组读取配置字段 |
| `GET` | `/api/config?fields=f1,f2,...` | 按字段名读取配置 |
| `GET` | `/api/config` | 读取全量配置 |
| `POST` | `/api/config` | 局部更新配置（JSON body，只写提交的字段） |

配置分组（`ConfigGroup`）：`wifi` / `llm` / `im` / `search` / `capabilities` / `skills` / `time`

每个分组对应的字段见 `http_server_config_api.c` 中的 `CONFIG_FIELDS` 表。

### 状态接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/status` | Wi-Fi 状态、IP、存储根路径 |
| `POST` | `/api/restart` | 重启设备 |

### 能力与模块接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/capabilities` | 列出所有已注册 Capability（含 group_id、显示名、默认可见性） |
| `GET` | `/api/lua-modules` | 列出所有已注册 Lua 模块 |

### 文件系统接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/files?path=<p>` | 列出目录内容 |
| `POST` | `/api/files/upload?path=<p>` | 上传/写入文件 |
| `POST` | `/api/files/mkdir` | 创建目录（body: `{"path":"...", "recursive":true}`） |
| `DELETE` | `/api/files?path=<p>&recursive=1` | 删除文件或目录 |
| `GET` | `/files<path>` | 直接读取文件内容（静态文件路由） |

### Web IM 接口（实时聊天）

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/webim/status` | Web IM 绑定状态 |
| `POST` | `/api/webim/send` | 发送消息（body: `{"chat_id":"...","text":"...","files":[]}`） |
| `WS` | `/ws/webim` | WebSocket 实时接收 Agent 回复事件 |

### WeChat 登录接口（可选）

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/wechat/login/start` | 启动微信扫码登录 |
| `GET` | `/api/wechat/login/status` | 轮询登录状态 |
| `POST` | `/api/wechat/login/cancel` | 取消登录 |

---

## 4. 前端适配方案

### 4.1 直接复用现有前端

**适用场景**：新项目的配置需求与 edge_agent 相同或高度相似。

1. 将 `http_server` 组件连同 `frontend_source/` 整体复制到新项目。
2. 按需修改 `frontend_source/src/pages/` 下的页面组件，添加或移除配置项。
3. 修改 `frontend_source/src/api/client.ts` 中的 `AppConfig` 类型和 `GROUP_FIELDS`，使其与新项目的配置字段保持一致。
4. 执行 `pnpm build` 重新生成 `dist/`，固件会通过 CMakeLists 将 `dist/` 中的文件内嵌为 C 数组。

```bash
cd <new_project>/components/http_server/frontend_source
pnpm install
pnpm build       # 生成 dist/
pnpm typecheck   # 类型检查
```

### 4.2 替换前端（自定义 UI）

**适用场景**：新项目需要全新的 UI 框架（如 Vue、原生 HTML、移动端 App）。

#### 后端保持不变

`http_server` 提供的 REST API（见第 3 节）是标准 JSON over HTTP，任何前端均可直接调用，无需修改后端代码。

#### 自定义前端接入方式

**选项 A：替换嵌入式前端资源**

1. 用你的构建工具输出静态 HTML/JS/CSS 到 `dist/` 目录。
2. 修改 `http_server_assets.c`，将新的 `dist/` 文件内嵌为 C 数组（`extern const uint8_t ... []`）。
3. 在 `http_server_register_assets_routes` 中更新路由表，将 `/` 和其他静态路径指向新资源。

**选项 B：独立 Web App（纯前端，不内嵌到固件）**

1. 独立部署前端（本地电脑、手机浏览器访问设备 IP）。
2. 通过 CORS 或同域请求访问 `http://<device_ip>/api/...`。
3. 完全绕过固件中的前端资源，后端不需要任何改动。

> **推荐**：调试阶段使用选项 B（快速迭代），生产阶段再考虑内嵌到固件。

**选项 C：移动端 App 或桌面客户端**

同选项 B，直接调用设备 HTTP API 即可。WebSocket `/ws/webim` 可以接收实时 Agent 回复，适合聊天类 UI。

### 4.3 前后端 API 契约

新前端需要遵守以下约定：

#### 配置读写

- **读取**：`GET /api/config?groups=<group_list>` 返回 `Partial<AppConfig>`（JSON 对象）
- **写入**：`POST /api/config`，body 为只包含需要更新字段的 JSON 对象（局部更新，不传的字段不被清空）
- 错误时返回 `{"error": "错误描述"}` + 非 2xx 状态码

#### WebSocket 消息格式

`/ws/webim` 推送的事件为 JSON 文本帧，包含字段：

```json
{
  "seq": 1,
  "role": "assistant",
  "text": "回复内容",
  "ts_ms": 1700000000000,
  "links": [{"url": "http://...", "label": "链接文字"}]
}
```

发送消息使用 REST：`POST /api/webim/send`，body 为：

```json
{"chat_id": "web", "text": "用户输入", "files": []}
```

#### 配置字段类型

所有配置字段均为**字符串类型**（bool 类型用 `"true"/"false"` 字符串表示），与 NVS 存储格式一致。前端展示时可自行转换为 checkbox 等控件。

---

## 5. 最小移植模板

以下是一个最简 `main.c` 骨架，展示移植 ESP-Claw 后端所需的最少代码：

```c
#include "nvs_flash.h"
#include "app_claw.h"
#include "claw_paths.h"
#include "http_server.h"
#include "wifi_manager.h"
// 你自己的文件系统和配置头文件
#include "my_fs.h"
#include "my_config.h"

static esp_err_t my_load_config(app_config_t *cfg) { return my_config_load(cfg); }
static esp_err_t my_save_config(const app_config_t *cfg) { return my_config_save(cfg); }
static esp_err_t my_get_wifi(http_server_wifi_status_t *s) {
    /* 填充 s->wifi_connected, s->ip 等 */
    return ESP_OK;
}
static esp_err_t my_save_claw_cfg(const app_claw_config_t *cfg, void *ctx) {
    return my_claw_config_save(cfg);
}

void app_main(void)
{
    nvs_flash_init();

    // 1. 挂载文件系统
    my_fs_init();
    claw_paths_set(CLAW_PATH_DATA,   my_fs_data_path());
    claw_paths_set(CLAW_PATH_SYSTEM, "/system");

    // 2. 可选：UI（屏幕/LED 表情）
    app_claw_ui_start();

    // 3. HTTP 服务
    http_server_init(&(http_server_config_t){
        .storage_base_path = my_fs_data_path(),
        .services = {
            .load_config     = my_load_config,
            .save_config     = my_save_config,
            .get_wifi_status = my_get_wifi,
            .restart_device  = esp_restart,
        },
    });

    // 4. 网络
    wifi_manager_init();
    wifi_manager_start(&(wifi_manager_config_t){ .sta_ssid = "MySSID", .sta_password = "pass" });
    http_server_start();

    // 5. 加载配置 → 启动 Agent
    app_claw_config_t claw_cfg = {0};
    my_load_claw_config(&claw_cfg);
    app_claw_set_save_config_callback(my_save_claw_cfg, NULL);
    app_claw_start(&claw_cfg);
}
```

---

## 6. 常见问题

### Q1：新项目没有 SYSTEM 分区怎么办？

SYSTEM 分区主要存放内置 Skill 和内置 Lua 脚本。如果新项目暂不需要这些功能：

- 将 `claw_paths_set(CLAW_PATH_SYSTEM, ...)` 指向一个空目录（如 DATA 下的 `/fatfs/.system_stub`）。
- 内置 Skill 不会加载，但基础 Agent 功能（LLM 对话、Capability 调用）仍可正常工作。

建议参照 `application/edge_agent/fatfs_image/` 为新项目准备一个最小 SYSTEM 镜像，至少包含 `.recovery/` 目录。

### Q2：如何只保留 LLM 对话，去掉 IM、Scheduler 等功能？

在 `sdkconfig.defaults` 中按需关闭（见 [2.6 可选能力裁剪](#26-可选能力裁剪)），同时在 `app_capabilities.c` 中对应的 `#if CONFIG_...` 块会自动不编译。

### Q3：前端需要添加新的配置字段，后端如何同步？

1. 在 `app_config_t`（`app_config.h`）中添加新字段。
2. 在 `http_server_config_api.c` 的 `CONFIG_FIELDS` 数组中注册新字段（指定字段名、所属分组、结构体偏移）。
3. 在 `app_config.c` 中添加该字段的 NVS 读写逻辑。
4. 若新字段需要传递给 Agent，在 `app_config_to_claw()` 中添加映射，并在 `app_claw_config_t` 中添加对应字段。
5. 前端 `client.ts` 中在 `AppConfig` 类型和 `GROUP_FIELDS` 中添加该字段。

### Q4：WebSocket Web IM 不工作？

确认以下几点：
- `sdkconfig` 中已开启 `CONFIG_HTTPD_WS_SUPPORT=y`。
- 调用了 `http_server_webim_bind_im()`（在 `app_claw_start` 之后）。
- `CONFIG_APP_CLAW_CAP_IM_LOCAL=y`（`cap_im_local` 能力已启用）。

### Q5：移植后设备内存不足？

- 使用 PSRAM（确保 `CONFIG_SPIRAM=y`）。
- 关闭不需要的 Lua 模块（视觉、音频等较占内存）。
- 减小 `claw_core` 的上下文窗口大小（通过 Kconfig 配置 `CLAW_CORE_MAX_CONTEXT_TOKENS`）。
- 检查各 FreeRTOS 任务栈大小是否合理。

---

*本文档基于 ESP-Claw 仓库（`application/edge_agent/`）代码分析编写，如有架构变更请以最新代码为准。*
