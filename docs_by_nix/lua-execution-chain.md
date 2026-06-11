# 智能体生成 Lua 脚本到 C 程序执行链路

## 流程图

```mermaid
flowchart TD
    A["用户输入 (IM/CLI/事件)"] --> B

    subgraph claw_core ["claw_core — 智能体主循环"]
        B["request_queue 接收请求\nclaw_core_agent_loop_task()"]
        B --> C["构建上下文\nclaw_core_build_iteration_context()\n收集 memory / skills / tools_json"]
        C --> D["调用 LLM HTTP\nclaw_core_llm_chat_messages()"]
        D --> E{{"LLM 返回 tool_calls?"}}
        E -- "否 (纯文本回复)" --> F["结束，发送响应给用户"]
        E -- "是 (tool_call=run_script)" --> G["claw_core_append_tool_results_messages()\n→ claw_cap_call_from_core('run_script', JSON)"]
        G --> H["把工具结果追加到消息，\n下一轮再次发送给 LLM"]
        H --> C
    end

    subgraph claw_cap ["claw_cap — 能力调度层"]
        G --> I["claw_cap_call()\n按 name 查找 descriptor slot\n权限检查 (CLAW_CAP_FLAG_CALLABLE_BY_LLM)"]
        I --> J["调用 descriptor.execute\n= cap_lua_run_script_execute()"]
    end

    subgraph cap_lua ["cap_lua — Lua 能力"]
        J --> K["解析 JSON 参数\n提取 path / timeout_ms / args"]
        K --> L["cap_lua_async_run_and_wait()\n提交 cap_lua_async_job_t"]
        L --> M["cap_lua_job_task()\n独立 FreeRTOS 任务"]
    end

    subgraph runtime ["cap_lua_runtime — Lua VM 执行"]
        M --> N["cap_lua_runtime_execute_file(path)"]
        N --> O["luaL_newstate()\n创建 Lua 虚拟机状态"]
        O --> P["luaL_openlibs() 加载标准库\ncap_lua_load_registered_modules()\n注册所有 C 模块 (gpio/i2c/audio...)"]
        P --> Q["cap_lua_set_args_global()\nJSON args → Lua table → args全局变量"]
        Q --> R["替换 print → 输出捕获回调\nlua_sethook() → 超时/停止 hook"]
        R --> S["luaL_dofile(path)\n加载 .lua 脚本并执行"]
    end

    subgraph vm_to_c ["Lua 脚本调用 C 模块"]
        S --> T["脚本调用 require('gpio')/require('i2c')..."]
        T --> U["luaopen_xxx() 已预注册\n返回 C 函数表"]
        U --> V["Lua 调用 gpio.set_level(pin, 1)\n→ 执行 C 函数 lua_driver_gpio_set_level(L)\n→ esp_gpio_set_level() 硬件 API"]
    end

    S --> W["脚本执行完毕\ncap_lua_run_exit_cleanups()\nlua_close(L)"]
    W --> X["输出字符串返回给 claw_core\n作为 tool_result JSON"]
    X --> G
```

---

## 各阶段详细说明

### 1. LLM 决策生成 `tool_call`

- `components/claw_modules/claw_core/src/claw_core_agent_loop.c` 的主循环在调用 LLM 前，通过 `claw_cap_build_llm_tools_json()` 把所有注册的 capability（包括 `run_script`、`run_script_async`）序列化为 JSON schema 发给 LLM。
- LLM 决定执行 Lua 脚本时，返回形如以下结构的 tool_call：

```json
{
  "name": "run_script",
  "input": {
    "path": "/fatfs/scripts/xxx.lua",
    "args": { "key": "value" }
  }
}
```

### 2. 能力调度 (`claw_cap`)

- `components/claw_modules/claw_cap/src/claw_cap.c` 的 `claw_cap_call_from_core()` → `claw_cap_call()` 根据名字在 descriptor 表里查找，做权限校验（是否标记 `CLAW_CAP_FLAG_CALLABLE_BY_LLM`），然后调用 `descriptor.execute`。

### 3. Lua 能力入口 (`cap_lua`)

- `components/claw_capabilities/cap_lua/src/cap_lua.c` 的 `cap_lua_run_script_execute()` 解析 JSON，构造 `cap_lua_async_job_t`，调用 `cap_lua_async_run_and_wait()` 同步等待结果。
- `run_script_async` 则不等待，立即返回 job_id，脚本在后台运行。

### 4. FreeRTOS 异步任务 (`cap_lua_async`)

- `components/claw_capabilities/cap_lua/src/cap_lua_async.c` 的 `cap_lua_job_task()` 是独立 FreeRTOS 任务，分配输出缓冲区后调用 `cap_lua_runtime_execute_file()`。

### 5. Lua VM 真正执行 (`cap_lua_runtime`)

- `components/claw_capabilities/cap_lua/src/cap_lua_runtime.c` 的 `cap_lua_runtime_execute_file()` 完成以下工作：

| 步骤 | 调用 | 说明 |
|------|------|------|
| 1 | `luaL_newstate()` | 创建 Lua 5.5 VM 状态（来自 `georgik/lua` 组件） |
| 2 | `luaL_openlibs()` | 加载 Lua 标准库 |
| 3 | `cap_lua_load_registered_modules()` | 逐个调用各 C 模块的 `luaopen_xxx()` 注册到 VM |
| 4 | `cap_lua_set_args_global()` | 把 JSON args 递归转换成 Lua table 赋给全局 `args` |
| 5 | 替换 `print` | 用捕获输出的闭包覆盖，所有 print 输出写入 output buffer |
| 6 | `lua_sethook()` | 注册超时/停止检查钩子（每 100 条指令触发一次） |
| 7 | `luaL_dofile()` | 加载并执行 `.lua` 文件 |

### 6. Lua 回调 C 硬件模块

- 脚本中 `require("gpio")` 调用预注册的 `luaopen_gpio()`，返回包含 C 函数指针的 Lua table。
- 调用链示例：

```
Lua: gpio.set_level(2, 1)
  → Lua VM 查找 gpio 模块 C 函数表
  → lua_driver_gpio_set_level(lua_State *L)         [components/lua_modules/lua_driver_gpio/]
  → esp_gpio_set_level(gpio_num, level)              [ESP-IDF GPIO API]
  → 硬件寄存器写入
```

### 7. 结果回传

- 脚本里所有 `print()` 的输出被捕获到 output buffer，脚本结束后作为 tool_result 字符串返回。
- `claw_core` 把这个结果追加到消息历史，再次调用 LLM，LLM 基于执行结果生成下一步动作或最终回复用户。

---

## 关键源文件索引

| 文件 | 职责 |
|------|------|
| `components/claw_modules/claw_core/src/claw_core_agent_loop.c` | 智能体主循环，LLM 调用与 tool_call 分发 |
| `components/claw_modules/claw_cap/src/claw_cap.c` | 能力注册、调度、权限控制 |
| `components/claw_capabilities/cap_lua/src/cap_lua.c` | Lua 能力描述符，`run_script` / `run_script_async` 入口 |
| `components/claw_capabilities/cap_lua/src/cap_lua_async.c` | FreeRTOS 异步 job 管理，环形日志缓冲 |
| `components/claw_capabilities/cap_lua/src/cap_lua_runtime.c` | Lua VM 生命周期，模块加载，超时钩子，文件执行 |
| `components/lua_modules/lua_driver_*/` | 各硬件外设的 Lua→C 绑定模块 |
| `components/lua_modules/lua_module_*/` | 音频、显示、BLE 等高级功能模块 |
| `components/common/app_claw/app_lua_modules.c` | 应用层 Lua 模块注册入口 |
