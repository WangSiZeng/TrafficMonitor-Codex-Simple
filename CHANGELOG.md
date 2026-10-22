# 版本记录

## V0.1.2 — Codex 独立完成

在 V0.1.1 的代码基础上继续迭代，添加额度截止时间的显示和控制，并完成 GitHub 项目整理与上传。

- Restore full default labels, `Codex 5h额度:` and `Codex 7d额度:`, while preserving saved custom text.
- Expand the Chinese README with actual screenshots and the implemented query, refresh, configuration and proxy behavior.

- Add an optional smaller reset-time display after each quota percentage, configurable in plugin settings.
- Preserve the original ChatGPT logo and host label, font, colors and value alignment; allow a separate freely editable timestamp size.
- Append the reset time directly after the percentage, keep historical-data status in the tooltip, and place time settings before graph settings.
- Refresh saved host display labels and their measured widths while reset-time drawing is enabled, without restarting TrafficMonitor.
- Center plugin settings in the current monitor's work area so taskbar parents cannot position it offscreen.
- Parse structured JSON, validate quota windows, and select the Codex limit bucket.
- Isolate inherited child-process handles and launch only resolved absolute executable paths.
- Keep the DLL loaded while its background query runs; remove waits from DllMain.
- Preserve data timestamps and mark historical data. Local session fallback is opt-in.
- Bound refresh intervals and query output. Avoid raw responses and diagnostic logging.
- Add synthetic regression tests, generic build scripts, and privacy-conscious release packaging.

## V0.1.1 — deepseek flash V4.1 独立完成

- 在额度显示基础上，添加资源占用图的控制功能。

## V0.1.0 — deepseek flash V4.1 独立完成

- 第一版，仅提供额度显示功能。
