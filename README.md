# Codex Quota Orb

**Ultra-light native Windows Codex quota monitor** — a draggable floating orb for weekly usage, reset countdown, and reset credits. No Electron, WebView, Node.js, Python, or .NET runtime.

**极致轻量的 Windows Codex 额度悬浮球**——显示周额度、重置倒计时与重置卡数量；随 Codex 自动显示，完全通过本机 Codex app-server 读取数据。

![Codex Quota Orb showing weekly remaining quota, reset countdown and reset credits](docs/screenshot.png)

## Why this project / 项目特点

- Tiny native Win32/C++ executable: about **189 KB**, statically linked.
- Circular weekly quota gauge with remaining percentage.
- Local countdown in `6天15时29分` format, calculated from Codex's reset timestamp.
- Displays available rate-limit reset credits.
- Soft translucent Windows 11-style UI following system light/dark mode and accent color.
- Draggable and edge-snapping; starts centered on the right edge of the primary work area.
- Right-click menu with only `置顶` (Always on top) and `关闭` (Close).
- Starts with Windows in low-overhead watch mode and appears only while Codex is running.
- Refreshes quota every 60 seconds; the countdown advances locally between refreshes.

## Quick install / 快速安装

1. Download and extract `CodexQuotaOrb-v0.1.0-win-x64.zip` from Releases.
2. Double-click `install.cmd`.
3. Start Codex. The orb appears automatically.

下载并解压 Release 压缩包，双击 `install.cmd`。无需管理员权限，程序安装到 `%LOCALAPPDATA%\CodexQuotaOrb`，并添加当前用户的登录自启动项。

Windows SmartScreen may warn about an unsigned community executable. Verify its SHA-256 from `SHA256SUMS.txt` or build it from source.

### Portable use / 免安装运行

Run `bin\CodexQuotaOrb.exe` directly. Portable mode shows the orb immediately and does not create an autostart entry.

### Uninstall / 卸载

Double-click `uninstall.cmd`, or run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

## Privacy and data source / 隐私与数据来源

Codex Quota Orb **does not read or copy `.codex/auth.json`** and does not make its own TCP/HTTP requests to OpenAI or ChatGPT.

It launches the locally installed Codex executable only during refresh:

```text
codex -s read-only -a never app-server --listen stdio://
```

It calls the read-only `account/rateLimits/read` method over standard input/output. The weekly window is selected by `windowDurationMins == 10080`; reset credits come from `rateLimitResetCredits.availableCount`.

程序只与本机 Codex app-server 的标准输入输出通信，不保存登录凭据，不直接连接 OpenAI 网络接口。

Protocol reference: [official Codex app-server rate-limits documentation](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt).

## Build from source / 从源码构建

Requirements: Windows 10/11, Visual Studio 2019 or later with **Desktop development with C++**, and CMake 3.20+.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

The Release build uses the static MSVC runtime and produces one standalone executable at `bin\CodexQuotaOrb.exe`.

To generate the distributable ZIP and checksum file:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
```

## Custom Codex path / 自定义 Codex 路径

The app checks `CODEX_ORB_CODEX_PATH`, then `PATH`, then common Codex Desktop installation folders.

```powershell
[Environment]::SetEnvironmentVariable(
  'CODEX_ORB_CODEX_PATH',
  'D:\path\to\codex.exe',
  'User'
)
```

## Troubleshooting / 常见问题

- **The orb does not appear:** confirm Codex Desktop is running. Installed `--watch` mode hides the orb when Codex is closed.
- **Quota stays unavailable:** run `codex --version` or set `CODEX_ORB_CODEX_PATH`.
- **Move the orb:** drag and release; it snaps to the nearest work-area edge and remembers that position.
- **Disable always-on-top or close:** right-click the orb and choose `置顶` or `关闭`.

## Search terms / 检索关键词

Related searches: **Codex usage monitor, Codex quota monitor, Codex rate-limit widget, Codex weekly usage, Codex remaining quota, Codex reset timer, Codex reset credits, Codex desktop widget, Codex floating window, Codex floating ball, Windows Codex widget, local Codex usage display, lightweight native Win32 utility**.

中文检索词：**Codex 额度、Codex 周额度、Codex 剩余额度、Codex 用量监控、Codex 限额监控、Codex 重置倒计时、Codex 重置卡、Codex 悬浮窗、Codex 悬浮球、Codex 桌面小组件、Windows 11 悬浮球、本地额度显示、轻量原生工具、随 Codex 启动**。

## Design references / 设计参考

Interaction and data-boundary research referenced:

- [codex-weekusage-tray](https://github.com/NotNull92/codex-weekusage-tray) (MIT)
- [codex-usage-ball](https://github.com/jiaheng6/codex-usage-ball) (MIT)
- [AI Quota Deck](https://github.com/JoshuaWang2211/ai-quota-deck) (MIT)
- [Codex Minibar](https://github.com/vertopolkaLF/codex-minibar) (Apache-2.0)

This repository is an independent native Win32 implementation.

## License and disclaimer

[MIT](LICENSE). Codex Quota Orb is an unofficial community project and is not affiliated with or endorsed by OpenAI.
