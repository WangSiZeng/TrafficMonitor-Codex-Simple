# TrafficMonitor Codex Simple V0.1.4

用于 Windows x64 TrafficMonitor 的 Codex 额度与重置时间插件。

## 更新内容

- **插件独立绘图**：额度图形由插件自行绘制，独立于 TrafficMonitor 的图形总开关。
- **图形设置**：支持自选颜色、跟随 Windows 主题颜色、柱状图和横向滚动图；剩余/已用选项只改变图形。
- **重置时间范围**：在总开关下新增并排的“全部 / 5h额度 / 7d额度”单选项，支持单独显示某个额度窗口的时间。隐藏时间时释放对应宽度，关闭总开关会保留选择。
- **滚动历史**：成功查询时采样，每列对应一个查询周期；查询失败或窗口缺失时留空，不将旧缓存和本地快照补写为新历史。重绘和图形开关不影响仍在执行的查询采样。
- **显示与兼容**：保留宿主自定义标签、字体、颜色和对齐，延续 V0.1.3 的旧版初始化兼容修复。
- **文档**：更新设置截图与 README，补充时间范围、独立图形和滚动历史的配置说明。

## 下载安装

直接下载附件中的 **CodexUsage.dll**。退出 TrafficMonitor，将其复制到程序的 `plugins` 目录，再重新启动。升级时保留现有配置。

需要 x64 TrafficMonitor，以及已使用 ChatGPT 账号登录的官方原生 Codex。插件静态链接 C/C++ 运行库和 MFC 调色板，首次安装只需一个 DLL。

| 附件 | 用途 |
| --- | --- |
| `CodexUsage.dll` | 可直接安装的 x64 插件 |
| `CodexUsage.dll.sha256` | DLL 校验文件 |
| `CodexUsage-LICENSE.txt` | 许可、责任限制和第三方声明 |
| `TrafficMonitor-Codex-Simple-0.1.4-win64.zip` | DLL、示例配置、README、截图和许可文件 |
| `TrafficMonitor-Codex-Simple-0.1.4-win64.zip.sha256` | 完整包校验文件 |

旧配置默认显示全部重置时间。可在设置窗口选择范围，或配置 `reset_time_scope=all|5h|7d`。查询间隔默认 300 秒，`refresh_seconds=120` 表示两分钟。

## 验证

779 项模拟回归检查通过，覆盖新旧初始化、时间范围、布局宽度、百分比、滚动历史、句柄隔离及 DLL 生命周期。设置窗口的互斥选择、保存/取消、字号、调色板和卸载重载检查通过。

TrafficMonitor 1.8.6.0 已验证加载、真实额度查询、绘制回调及自定义标签同步。1.8.5.1 的验证范围为模拟回归。

DLL 未进行代码签名。源码采用 MIT 许可证，第三方组件适用各自许可；责任限制见随附 LICENSE、DISCLAIMER.md 和第三方声明。
