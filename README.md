# CodexUsage for TrafficMonitor

在 Windows 任务栏显示 Codex 的 **5 小时 / 7 天额度**。默认显示剩余百分比，支持资源占用图和已用/剩余模式。

这是社区插件，与 OpenAI、TrafficMonitor 官方无隶属关系。

## 开发说明

本项目的代码基础由 **deepseek flash4.1** 完成，后期由 **Codex** 接管进行编写和完善。

## 安装

1. 使用 64 位 Windows 和支持插件 API 7 的 64 位 TrafficMonitor。
2. 安装官方 Codex CLI 或 Codex 桌面应用，并在本机使用需要查询的 ChatGPT 账号登录。
3. 将发布包中的 `CodexUsage.dll` 放入 TrafficMonitor 的 `plugins` 目录，重启 TrafficMonitor。
4. 在任务栏窗口的“显示设置”中勾选 `Codex 5小时额度` 和 `Codex 7天额度`。

鼠标悬停可查看数据来源、时间和重置时间。在 `5h` / `7d` 显示区域右键，会出现 TrafficMonitor 自动提供的“Codex 额度”菜单；其“选项”可设置时间显示、时间字号及资源占用图。

## 配置

将 `CodexUsage.example.ini` 复制为 **TrafficMonitor 插件配置目录**中的 `CodexUsage.ini`。配置目录由宿主提供，便携模式与非便携模式可能不同，并不一定是 DLL 所在目录。

```ini
[config]
refresh_seconds=120
show_remaining=1
graph_enabled=0
graph_mode=remaining
show_reset_time=0
time_font_size=10
allow_local_fallback=0
```

- `refresh_seconds`：默认 300 秒，最小 30 秒，最大 86400 秒。`120` 表示约每两分钟触发一次后台查询。启动时立即查询一次。修改 INI 后重启 TrafficMonitor。
- `show_remaining`：`1` 显示剩余；`0` 显示已用。
- `graph_enabled`：是否启用资源占用图。
- `graph_mode`：`remaining` 或 `used`，独立于文字显示模式。
- `show_reset_time`：默认 `0`。可在插件设置中勾选“额度后显示重置时间”；开启后，在原有额度样式后追加时间，5 小时窗口显示 `HH:mm`，7 天窗口显示 `MM-dd HH:mm`，使用本机时区。
- `time_font_size`：时间文字的字号，默认 10 磅。设置中的“字体大小”可自由输入正整数，也可用右侧上下箭头调整。额度字体和对齐仍沿用宿主原有样式。
- `codex_path`：可选，指定可信的 `codex.exe` **绝对路径**。不支持 `.cmd` / `.bat` 启动脚本，也不从当前工作目录搜索程序。
- `allow_local_fallback`：默认 `0`。设为 `1` 后，在首次服务端查询失败且没有可保留的数据时，尝试读取本地 rollout 事件中的历史额度。
- `sessions_dir`：仅用于已开启的本地回退。未设置时使用 `CODEX_HOME\sessions`，否则使用当前用户的 `.codex\sessions`。

开启时间显示后，修改 TrafficMonitor 的“显示文本设置”并应用，插件会自动同步已保存的标签和显示宽度，无需重启。主窗口与任务栏窗口的标签分别保留。

`--` 表示该窗口没有可用数据。上次查询失败保留的结果或本地会话快照，会在悬停说明中标记为“历史数据”，额度百分比后不添加状态符号。历史数据不会被标记为刚更新。

## 数据与隐私

插件使用 `codex app-server` 的标准输入输出协议，仅发送初始化和 `account/rateLimits/read` 请求，不创建对话或执行模型任务。认证、令牌存储、刷新及与服务端通信均由本机 Codex 负责。插件本身不读取、复制或上传 `auth.json`，不记录原始响应或诊断日志，不提供遥测或远程控制端口。

额度查询仍会使 Codex 向其配置的服务端发送已认证请求，并可能更新它自己的登录缓存、数据库或日志；服务端地址和代理等沿用本机 Codex 配置。请仅使用可信的 Codex 程序与配置，通常无需以管理员身份运行 TrafficMonitor。

若主动开启本地回退，插件会在内存中读取少量会话文件尾部，只解析 `event_msg/token_count` 中的额度事件，不保存或传送会话正文。该快照不保证新鲜，也不能可靠识别切换账号之前的数据；切换账号后请重启 TrafficMonitor。

使用同一个 `CODEX_HOME` 和相同登录存储才能查询预期账号；桌面应用和 CLI 的账号并非在所有安装方式下都自动一致。API key 额度不属于此插件支持的 ChatGPT 订阅额度。仅显示 300 分钟和 10080 分钟窗口，其他时长不会被误标为 5 小时或 7 天。

Codex app-server 仍为实验性集成，CLI 升级可能需要更新插件。参考 [OpenAI app-server 文档](https://learn.chatgpt.com/docs/app-server) 和 [认证文档](https://learn.chatgpt.com/docs/auth)。

## 构建与测试

需要 Visual Studio 2022 / Build Tools，安装“使用 C++ 的桌面开发”工作负载。构建脚本通过 `vswhere` 定位工具链。

```bat
build.bat
test_build.bat
```

输出为 `build\CodexUsage.dll`。测试只使用合成数据和模拟 app-server，验证解析边界、配置、子进程句柄隔离和后台查询期间 DLL 卸载，不读取真实登录或会话。

GitHub Actions 构建配置保存在 `ci/build.yml.example`，仅作为示例，不会自动运行。需要启用时，将其放入 `.github/workflows/build.yml`，并使用具有相应 workflow 权限的凭据提交。

可选的真实只读验证：

```bat
build\backend_tests.exe --live "C:\Tools\Codex\codex.exe"
```

该测试只打印查询是否成功及额度窗口，不打印 token、账号标识、原始响应或 stderr。它会使用所指定 Codex 的真实登录状态与网络。

## 许可

本项目采用 [MIT 许可证](LICENSE)，允许商业使用、修改、再分发和销售软件副本；再分发时须保留版权及许可声明。TrafficMonitor 插件接口和 yyjson 同样采用 MIT，其版权声明及许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本项目不包含 TrafficMonitor 主程序源码；主程序的许可不因本插件而改变。

在适用法律允许的最大范围内，本软件按“原样”提供，不提供任何担保；作者及版权持有者不承担因使用本插件产生的经济损失、数据丢失、业务中断、额外费用或其他损害责任。完整条款见 [LICENSE](LICENSE)，中文说明见 [DISCLAIMER.md](DISCLAIMER.md)。

源码中第三方作者的署名和联系地址属于其公开版权声明。
