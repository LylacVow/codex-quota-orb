# Codex Quota Orb v0.2.0

The ultra-light system-tray edition.

## What changed

- Replaced the floating orb with one weekly remaining-percentage icon beside the Windows clock
- Removed transparency, animation, countdown, reset-card display, dragging, edge snapping, and per-second painting
- Keeps a hidden native process resident with almost no idle CPU activity
- Detects Codex every 10 seconds and synchronizes every 60 seconds only while Codex is running
- Makes no app-server request while Codex is closed
- Keeps the last successful percentage in the tray and across restarts
- Rebuilds the icon only when the quota value or Windows system theme changes
- Preserves a stable tray identity so Windows can remember the user's icon placement

## 中文简介

此版本改为 Windows 系统托盘精简版：任务栏时钟左侧只显示周额度剩余百分比。程序常驻，但 Codex 未运行时不启动 app-server、不进行额度通信；检测到 Codex 后每 60 秒同步一次。已移除悬浮窗、透明渲染、倒计时、重置卡、拖动吸附和每秒重绘，以进一步降低性能占用。

## Install

Download and extract `CodexQuotaOrb-v0.2.0-win-x64.zip`, then double-click `install.cmd`. Administrator privileges are not required.

If Windows initially places the icon in the notification overflow panel, drag it beside the clock once.
