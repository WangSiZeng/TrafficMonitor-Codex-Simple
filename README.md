# TrafficMonitor Codex Simple

用于 Windows x64 **TrafficMonitor** 的 Codex 额度插件，在任务栏显示 **5 小时 / 7 天剩余额度**，支持重置时间和额度图形。

[V0.1.4 Release](https://github.com/WangSiZeng/TrafficMonitor-Codex-Simple/releases/tag/v0.1.4) · [下载 DLL](https://github.com/WangSiZeng/TrafficMonitor-Codex-Simple/releases/latest/download/CodexUsage.dll) · [完整安装包](https://github.com/WangSiZeng/TrafficMonitor-Codex-Simple/releases/latest) · [版本记录](CHANGELOG.md)

本项目为社区插件，与 OpenAI、TrafficMonitor 官方无隶属关系。

## 显示效果

![任务栏中的 Codex 额度和重置时间](docs/images/taskbar-usage.png)

| 显示项目 | 默认文本 | 内容 |
| --- | --- | --- |
| Codex 5小时额度 | `Codex 5h额度:` | 5 小时窗口的剩余百分比，重置时间格式为 `HH:mm` |
| Codex 7天额度 | `Codex 7d额度:` | 7 天窗口的剩余百分比，重置时间格式为 `MM-dd HH:mm` |

时间使用系统本地时区，追加在百分号之后。鼠标悬停可查看已用和剩余比例、重置时间、套餐、数据来源及更新时间。`--` 表示尚无可用数据；缓存结果的历史状态显示在悬停说明中。

## 设置

![V0.1.4 插件设置：重置时间范围、字号和额度图形](docs/images/plugin-settings.png)

- **重置时间显示**：总开关。并排的“全部 / 5h额度 / 7d额度”选择显示范围；未选中项目不预留时间宽度。关闭总开关后保留所选范围。
- **字体大小**：时间文字的字号，单位为磅。支持输入正整数或使用上下箭头调整。
- **启用资源占用图**：由插件绘制任务栏额度图形，可独立于 TrafficMonitor 的图形总开关使用。
- **占用图颜色**：点击色块打开标准 / 自定义颜色面板；也可跟随 Windows 主题颜色。手动颜色会在切换后保留。
- **占用图类型**：柱状图按当前比例填充背景；横向滚动图展示历次成功查询的额度。
- **数据逻辑**：选择图形展示剩余额度或已用额度，切换时同时换算已有历史。**这组选项只影响图形，任务栏文字默认显示剩余百分比。**

点击“确定”应用并保存，取消或关闭窗口则放弃修改。时间和图形设置彼此独立；前面的显示文本、额度字体、颜色及数值对齐沿用宿主设置。

## 安装与升级

需要：

- 64 位 Windows 和 64 位 TrafficMonitor。
- 本机可运行的官方原生 `codex.exe`。
- 已在 Codex 中使用需要查询额度的 ChatGPT 账号登录。

安装步骤：

1. 下载 Release 附件中的 **`CodexUsage.dll`**。
2. 退出 TrafficMonitor，将 DLL 放入程序的 `plugins` 目录。
3. 重新启动 TrafficMonitor，在“其他功能 → 插件管理”确认“Codex 额度”已加载。
4. 在任务栏显示设置中勾选 `Codex 5小时额度` 和 `Codex 7天额度`。
5. 右键额度显示区域，通过“Codex 额度 → 选项”调整插件设置。

```text
TrafficMonitor/
├─ TrafficMonitor.exe
└─ plugins/
   ├─ CodexUsage.dll
   └─ CodexUsage.ini       # 可选；首次安装时无需创建
```

首次安装只需一个 DLL，无需额外安装插件运行库。C/C++ 运行库和 MFC 调色板均采用静态链接。x86 TrafficMonitor 无法加载本项目的 x64 DLL。

升级时退出 TrafficMonitor，替换 DLL 并保留现有配置。完整 ZIP 另含示例配置、文档、截图和许可证，其 DLL 与单独下载的同版文件一致。`.sha256` 文件用于完整性校验；再分发时请保留许可及第三方声明。

### 兼容性

插件同时支持 TrafficMonitor 1.8.5.1 的 `EI_CONFIG_DIR` 配置目录通知和新版 API 7 的 `OnInitialize` 初始化回调。

新旧回调流程已通过模拟回归测试，TrafficMonitor 1.8.6.0 已验证插件加载、额度查询、绘制回调和自定义文本同步。1.8.5.1 的验证范围为模拟测试。

## 数据来源

通过本机 Codex app-server 的 JSON-RPC 标准输入/输出协议读取额度：

```text
TrafficMonitor
  └─ CodexUsage.dll 后台线程
      └─ codex.exe app-server
          ├─ initialize → 等待初始化响应
          ├─ initialized
          └─ account/rateLimits/read → 读取额度
```

每轮查询启动一个隐藏子进程，读取结果后关闭输入管道并回收进程。插件只发送初始化和额度查询请求，不发送提示词或启动模型任务。

认证、令牌刷新和网络请求由 Codex 处理。插件不直接读取 `auth.json`，也不自行调用 HTTP 用量接口。API key 的计费额度不在支持范围内。

### 程序定位与账号环境

未指定 `codex_path` 时，插件从当前用户的 Codex 桌面安装目录、已注册的应用包路径和 `PATH` 中的绝对目录查找程序。仅接受原生 `.exe`，不执行 `.cmd`、`.bat`，不从当前工作目录搜索。

子进程继承 TrafficMonitor 的环境，包括 `CODEX_HOME`。请确保该环境指向所需账号的登录存储；切换账号或修改环境变量后重新启动 TrafficMonitor。显式配置的程序路径失效时，需要修正 `codex_path`；自动探测的桌面安装路径失效时会重新探测。

### 额度解析

| 返回字段 | 处理方式 |
| --- | --- |
| `rateLimitsByLimitId.codex` | 优先选择 Codex 限额桶 |
| `rateLimits` | 兼容单桶结果；拒绝明确属于其他产品的限额 |
| `windowDurationMins` / `window_minutes` | 按实际时长归类：300 分钟为 5 小时，10080 分钟为 7 天 |
| `usedPercent` / `used_percent` | 校验 0～100 的有限数值，文字默认换算为剩余百分比 |
| `resetsAt` / `resets_at` | 校验 Unix 时间戳，按本地时区显示 |
| 缺失或 `null` 窗口 | 两项独立处理，缺失项显示 `--` |

窗口按时长识别，不依赖其位于 `primary` 还是 `secondary`。

协议参考：[Codex app-server](https://learn.chatgpt.com/docs/app-server)。

## 刷新、历史与失败处理

- 启动时立即查询，之后由 TrafficMonitor 数据回调触发后台刷新。默认间隔为 **300 秒**，可通过 `refresh_seconds` 设置为 30～86400 秒；实际触发受宿主调度影响。
- 同时最多执行一轮查询，绘图线程不等待网络。协议响应等待上限为 20 秒，进程清理另有短暂等待。
- 查询失败时保留最近一次有效结果及原更新时间，在悬停说明中标为“历史数据”；成功后更新。
- 尚无有效结果时显示 `--`。到达重置时间后，额度仍以查询结果为准。
- 额度和滚动历史只保存在内存，重启后重新获取。

横向滚动图每列对应一个 `refresh_seconds` 周期，默认每列 5 分钟，仅记录实时查询成功的窗口数据。失败、窗口缺失或暂停查询的周期留空，缓存值和本地快照不会补写为新历史。采样与重绘分离；图形关闭、切换类型或窗口隐藏时，仍在执行的查询继续记录。每项最多保留 4096 条样本，剩余/已用切换直接换算已保存的比例。

### 可选本地回退

默认 `allow_local_fallback=0`。启用后，查询失败时会尝试读取本地会话尾部的 `event_msg/token_count.rate_limits`，仅在内存没有有效额度时采用。

本地结果属于历史快照，可能来自早期会话或其他账号。插件保留其原时间并标记历史状态，不将它写入实时滚动历史。

## 配置

配置文件名为 `CodexUsage.ini`，实际目录由 TrafficMonitor 提供。便携模式通常为 `plugins`；其他模式使用宿主的用户配置目录。可将 `CodexUsage.example.ini` 复制到该目录后重命名。

以下配置每两分钟查询，显示两项重置时间，并开启剩余额度柱状图：

```ini
[config]
refresh_seconds=120
show_remaining=1
show_reset_time=1
reset_time_scope=all
time_font_size=8
graph_enabled=1
graph_mode=remaining
graph_color=12632256
graph_follow_theme=0
graph_type=bar
allow_local_fallback=0
; 可选：可信原生程序的绝对路径
; codex_path=C:\Tools\Codex\codex.exe
; 可选：本地回退目录
; sessions_dir=C:\Data\Codex\sessions
```

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `refresh_seconds` | `300` | 查询间隔，单位秒；范围 30～86400 |
| `show_remaining` | `1` | 文字显示的高级 INI 项：`1` 剩余，`0` 已用；独立于设置页的图形数据逻辑 |
| `show_reset_time` | `0` | 重置时间显示总开关 |
| `reset_time_scope` | `all` | `all` 全部，`5h` 仅 5 小时额度，`7d` 仅 7 天额度；总开关关闭时均隐藏 |
| `time_font_size` | `10` | 时间字号，单位磅，支持正整数 |
| `graph_enabled` | `0` | 插件图形开关，独立于宿主图形总开关 |
| `graph_mode` | `remaining` | 图形数据逻辑：`remaining` 剩余，`used` 已用 |
| `graph_color` | `12632256` | 自选颜色，Windows `COLORREF` 十进制；默认 RGB(192,192,192) |
| `graph_follow_theme` | `0` | 是否跟随 Windows 主题颜色 |
| `graph_type` | `bar` | `bar` 柱状图，`scroll` 横向滚动图 |
| `allow_local_fallback` | `0` | 是否允许本地会话回退 |
| `codex_path` | 自动探测 | 可信原生 `codex.exe` 的绝对路径 |
| `sessions_dir` | 自动选择 | 本地回退目录，优先使用 `CODEX_HOME\sessions`，否则为用户的 `.codex\sessions` |

INI 在插件启动时读取，手动编辑后需重启 TrafficMonitor。在设置窗口点击“确定”后立即保存并生效。未指定或无法识别的 `reset_time_scope` 按 `all` 处理。

### 宿主显示文本

在 TrafficMonitor“选项 → 显示设置 → 显示文本设置”中自定义标签。已保存文本优先，包括主动清空的内容；主窗口与任务栏分别处理。

插件自绘时沿用宿主字体、文字颜色、数值对齐和标签。修改文本后点击“应用”或“确定”，标签和宽度会自动同步。每个项目只有启用自己的时间显示或图形时才使用自绘；隐藏时间的项目不保留时间宽度。

标签从插件配置目录上一级的 `config.ini` 读取，对应 `[plugin_display_str_taskbar_window]` 和 `[plugin_display_str_main_window]`。只读检查间隔为 250 毫秒，文件未变化时不重复解析。插件只保存自己的配置。

## 网络与隐私

网络请求由 Codex 处理，插件没有独立的 HTTP / SOCKS5 代理设置。代理行为取决于所用 Codex 版本、其配置和继承环境；请先确认同一环境中的 Codex 可以正常访问服务。

- 结构化解析 JSON，限制响应大小并匹配 JSON-RPC 请求 ID。
- 子进程只继承所需的标准输入、输出及诊断丢弃句柄。
- 后台查询持有 DLL 引用，结束后释放，防止执行期间被卸载。
- 插件无遥测、监听端口和原始响应日志，不保存账号凭据或额度历史文件。
- Codex 自身可能维护登录缓存、数据库和日志。
- 启用本地回退会读取会话尾部，其中可能包含正文；发布包不包含真实配置、凭据或会话文件。
- 指定程序以 TrafficMonitor 的权限运行，应使用可信的 Codex 程序和配置。

详情见 [SECURITY.md](SECURITY.md)。

## 构建与测试

需要 Visual Studio 2022 或 Build Tools、“使用 C++ 的桌面开发”工作负载，以及 **C++ MFC（x86 和 x64）** 组件。脚本通过 `vswhere` 定位工具链。

```bat
build.bat
test_build.bat
test_options.bat
```

- `build.bat`：生成 `build\CodexUsage.dll`。
- `test_build.bat`：运行模拟额度、配置、句柄隔离、DLL 生命周期、宿主兼容和绘图回归测试，不读取真实账号。
- `test_options.bat`：验证测试程序自身设置窗口的布局、确认/取消、时间范围、字号、调色板和图形设置。

可选的真实账号查询测试：

```bat
build\backend_tests.exe --live "C:\Tools\Codex\codex.exe"
```

此模式使用指定 Codex 的登录和网络，只打印查询状态和额度比例。

### 打包

```powershell
.\scripts\check-release.ps1
.\scripts\package.ps1
```

隐私检查面向 Git 暂存区的发布文件和构建 DLL，执行前需暂存待发布的修改。打包生成独立 DLL、SHA-256 文件、许可声明及完整 ZIP；`-DllOnly` 可只生成单 DLL 配套文件。DLL 未进行代码签名。

`ci/build.yml.example` 提供可选的 GitHub Actions 模板。

### 源码结构

| 文件 | 职责 |
| --- | --- |
| `CodexUsage.cpp` | 插件接口、查询调度、设置、文本同步和时间绘制 |
| `UsageBackend.cpp` / `.h` | 程序定位、子进程协议、额度解析和本地回退 |
| `GraphDrawing.h` | 有容量上限的滚动历史与图形绘制 |
| `ColorPicker.cpp` / `.rc` | 静态链接的标准 / 自定义颜色面板 |
| `PluginInterface.h` | TrafficMonitor API 7 接口 |
| `third_party/yyjson/` | JSON 解析器与许可证 |
| `tests/`、`test_options.cpp` | 模拟后端与设置窗口测试 |
| `docs/images/` | 实际界面截图 |

## 常见问题

| 现象 | 检查方式 |
| --- | --- |
| 插件未加载 | 确认宿主为 x64，DLL 放在 `plugins`，替换后重启 |
| 两项显示 `--` | 检查 Codex 的 ChatGPT 登录、原生程序路径、账号环境和网络 |
| 只有一项显示 `--` | 服务可能未提供对应时长的额度窗口 |
| 百分比长时间不更新 | 查看悬停说明的历史状态和数据时间，核对查询间隔 |
| 仅一项显示重置时间 | 检查“全部 / 5h额度 / 7d额度”的选择 |
| 滚动图只有少量列 | 历史从启动后逐次查询积累；每列代表一个查询周期 |
| 自定义文本未同步 | 在宿主应用或保存修改；使用 V0.1.3 或更新版本 |
| Codex 更新后查询失败 | 等待自动探测，或修正显式 `codex_path`；上游协议变更可能需要插件更新 |

## 开发记录

V0.1.0、V0.1.1 由 **deepseek flash V4.1 独立完成**；V0.1.2 起由 **Codex 独立完成**。各版本功能见 [CHANGELOG.md](CHANGELOG.md)。

## 许可

源码采用 [MIT 许可证](LICENSE)，允许商业使用、修改和再分发，须保留适用的版权及许可声明。TrafficMonitor 接口、yyjson 和 MFC 的声明及许可适用范围见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本仓库不包含 TrafficMonitor 主程序。

在适用法律允许的最大范围内，软件按“原样”提供，不提供担保；作者及版权持有者不对使用软件导致的经济损失、数据丢失、业务中断或其他损害承担责任。完整条款见 [LICENSE](LICENSE) 和 [DISCLAIMER.md](DISCLAIMER.md)。第三方在线服务适用各自条款。
