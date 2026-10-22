# TrafficMonitor Codex Simple V0.1.2

用于 Windows x64 TrafficMonitor 的简洁插件，专注显示 Codex 5 小时 / 7 天额度与截止（重置）时间。

开发说明：

- V0.1.0：由 **deepseek flash V4.1 独立完成**，仅提供额度显示功能。
- V0.1.1：由 **deepseek flash V4.1 独立完成**，添加资源占用图的控制功能。
- V0.1.2：由 **Codex 独立完成**，在前两版基础上添加截止时间显示及其开关、字号控制，完成 GitHub 整理上传。

- Queries the official local Codex app-server; defaults to a 300-second interval.
- Displays remaining-quota text and an optional smaller reset timestamp. The settings dialog's remaining/used controls affect only the resource graph, not the quota text.
- Preserves host display labels, fonts, colors and alignment; saved label edits synchronize without restarting.
- Timestamp font size is freely editable with a numeric input and up/down arrows; settings are centered on screen.
- Hardened JSON parsing, absolute executable resolution, inherited handles and DLL lifetime.
- Local conversation-file fallback is opt-in; historical data is clearly marked.
- No plugin telemetry, raw-response logging or bundled authentication files.

## 下载安装

推荐直接下载本页附件中的 **`CodexUsage.dll`**。退出 TrafficMonitor，将其放入 `TrafficMonitor.exe` 旁的 `plugins` 文件夹，然后重新启动并启用两个 Codex 显示项目即可。

首次安装无需预先创建 INI，也不需要随附其他插件依赖 DLL。需要事先安装 x64 TrafficMonitor 和官方 Codex，并已在 Codex 中用 ChatGPT 账号登录。

- `CodexUsage.dll`：可直接安装的 Windows x64 插件，与完整 ZIP 内的 DLL 相同。
- `CodexUsage.dll.sha256`：独立 DLL 的 SHA-256 校验文件。
- `CodexUsage-LICENSE.txt`：许可证、免责声明及第三方声明；不属于运行依赖，再分发时请保留适用声明。
- `TrafficMonitor-Codex-Simple-0.1.2-win64.zip`：包含插件、示例配置、文档、截图及许可文件的完整包。
- `.zip.sha256`：完整包的 SHA-256 校验文件。

Copy `CodexUsage.example.ini` to the plugin configuration directory as `CodexUsage.ini` if desired.
Set `refresh_seconds=120` for a two-minute interval.

Requires a trusted official native `codex.exe` and an existing ChatGPT login.
Codex app-server remains experimental, so future CLI updates may require compatibility changes.

Validation: 50 synthetic regression checks, settings confirm/cancel and font-input checks, rendered checks of reset timestamps and edited host labels, and a prior successful read-only query using the local Codex installation.

The release ZIP includes licenses and SHA-256 checksums. The DLL is not code-signed.

Licensed under MIT: commercial use, modification and redistribution are permitted with the applicable copyright and license notices retained. The software is provided as is without warranty, with liability limited as stated in LICENSE and explained in DISCLAIMER.md, to the maximum extent permitted by applicable law.
