# CodexUsage 1.2.0

Windows x64 plugin for displaying Codex's 5-hour and 7-day quota in TrafficMonitor.

开发说明：代码基础由 deepseek flash4.1 完成，后期由 Codex 接管进行编写和完善。

- Queries the official local Codex app-server; defaults to a 300-second interval.
- Supports remaining/used percentages, resource graphs and an optional smaller reset timestamp.
- Preserves host display labels, fonts, colors and alignment; saved label edits synchronize without restarting.
- Timestamp font size is freely editable with a numeric input and up/down arrows; settings are centered on screen.
- Hardened JSON parsing, absolute executable resolution, inherited handles and DLL lifetime.
- Local conversation-file fallback is opt-in; historical data is clearly marked.
- No plugin telemetry, raw-response logging or bundled authentication files.

Install `CodexUsage.dll` into TrafficMonitor's `plugins` folder and restart.
Copy `CodexUsage.example.ini` to the plugin configuration directory as `CodexUsage.ini` if desired.
Set `refresh_seconds=120` for a two-minute interval.

Requires a trusted official native `codex.exe` and an existing ChatGPT login.
Codex app-server remains experimental, so future CLI updates may require compatibility changes.

Validation: 48 synthetic regression checks, settings confirm/cancel and font-input checks, rendered checks of reset timestamps and edited host labels, and a successful read-only query using the local Codex installation.

The release ZIP includes licenses and SHA-256 checksums. The DLL is not code-signed.

Licensed under MIT: commercial use, modification and redistribution are permitted with the applicable copyright and license notices retained. The software is provided as is without warranty, with liability limited as stated in LICENSE and explained in DISCLAIMER.md, to the maximum extent permitted by applicable law.
