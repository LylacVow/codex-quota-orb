# Codex Quota Orb

**Ultra-light native Windows tray indicator for Codex weekly quota.** It places one percentage beside the Windows clock, stays resident at almost zero idle cost, and communicates with Codex only while Codex is running.

**极致轻量的 Windows Codex 周额度托盘显示。** 常驻任务栏时钟左侧，只显示一个剩余百分比；仅在 Codex 已运行时才启动本机 app-server 同步额度。

## v0.2 tray edition / 托盘精简版

- One dynamic percentage icon in the Windows notification area.
- Weekly remaining quota only; no floating window, countdown, reset-card row, animation, transparency, dragging, snapping, or per-second repaint.
- Hidden native Win32/C++ process with no Electron, WebView, Node.js, Python, or .NET runtime.
- Checks for a running Codex process every 10 seconds.
- Refreshes quota every 60 seconds **only while Codex is running**.
- When Codex is closed, it performs no app-server communication and keeps showing the last cached percentage.
- The icon is regenerated only when the value or Windows theme changes.
- Right-click the percentage icon and select `关闭` to exit.

Windows controls notification-area ordering. If the icon first appears in the overflow panel, drag it beside the clock once; its stable icon identity lets Windows remember that placement.

系统托盘图标的排列由 Windows 管理。如果首次出现在“隐藏的图标”区域，将它拖到时钟左侧一次即可，Windows 会记住位置。

## Quick install / 快速安装

1. Download and extract `CodexQuotaOrb-v0.2.0-win-x64.zip` from Releases.
2. Double-click `install.cmd`.
3. The percentage icon remains resident after sign-in and synchronizes when Codex starts.

下载并解压 Release 压缩包，双击 `install.cmd`。无需管理员权限；程序安装到 `%LOCALAPPDATA%\CodexQuotaOrb` 并加入当前用户登录自启动。

Windows SmartScreen may warn about an unsigned community executable. Verify `SHA256SUMS.txt` or build from source if preferred.

### Portable use / 免安装运行

Run `bin\CodexQuotaOrb.exe` directly. Portable mode does not create an autostart entry.

### Uninstall / 卸载

Double-click `uninstall.cmd`, or run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

## Privacy and data source / 隐私与数据来源

Codex Quota Orb **does not read or copy `.codex/auth.json`** and has no direct OpenAI/ChatGPT HTTP client.

Only after detecting `codex.exe` or `codex-code-mode-host.exe`, it briefly launches the locally installed executable in read-only mode:

```text
codex -s read-only -a never app-server --listen stdio://
```

It calls `account/rateLimits/read` over inherited standard-input/output pipes. The weekly window is selected by `windowDurationMins == 10080`. No Codex process means no quota request.

程序只在检测到 Codex 正在运行后，才通过本机 Codex app-server 的标准输入输出读取周额度；不保存登录凭据，不直接请求 OpenAI 网络接口。

Protocol reference: [official Codex app-server rate-limits documentation](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt).

## Build from source / 从源码构建

Requirements: Windows 10/11, Visual Studio 2019 or later with **Desktop development with C++**, and CMake 3.20+.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
```

The Release build uses the static MSVC runtime and produces one standalone executable.

## Custom Codex path / 自定义 Codex 路径

The app checks `CODEX_ORB_CODEX_PATH`, then `PATH`, then common Codex Desktop installation folders.

```powershell
[Environment]::SetEnvironmentVariable(
  'CODEX_ORB_CODEX_PATH',
  'D:\path\to\codex.exe',
  'User'
)
```

## Search terms / 检索关键词

Related searches: **Codex tray icon, Codex system tray, Codex taskbar widget, Codex weekly quota, Codex remaining percentage, Codex usage monitor, Codex quota monitor, Codex rate-limit indicator, Windows Codex status, local Codex usage display, lightweight native Win32 utility**.

中文检索词：**Codex 额度、Codex 周额度、Codex 剩余百分比、Codex 用量监控、Codex 限额监控、Codex 系统托盘、Codex 任务栏、Codex 时钟左侧、Codex 底栏显示、本地额度显示、极致轻量、低性能占用、随 Codex 同步**。

## Earlier floating-orb release / 早期悬浮球版本

Version `v0.1.0` remains available in Releases for users who prefer the draggable circular gauge, reset countdown, and reset-credit display. Version `v0.2.0` focuses on minimum visual and runtime overhead.

## License and disclaimer

[MIT](LICENSE). Codex Quota Orb is an unofficial community project and is not affiliated with or endorsed by OpenAI.
