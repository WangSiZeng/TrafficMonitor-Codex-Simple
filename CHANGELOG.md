# Changes

## 1.2.0

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
