# Codex Quota Orb v0.1.0

First public release of an ultra-light native Windows Codex quota monitor.

## Highlights

- About 189 KB standalone native Win32/C++ executable
- Weekly remaining-quota ring, reset countdown, and reset-credit count
- Translucent Windows 11-style UI following the system theme and accent color
- Dragging, edge snapping, remembered position, and always-on-top option
- Low-overhead watch mode following the Codex process
- 60-second quota refresh with a locally updated countdown
- Local Codex app-server source; no direct OpenAI network requests and no reading of `.codex/auth.json`

## Install

Download and extract `CodexQuotaOrb-v0.1.0-win-x64.zip`, then double-click `install.cmd`. Administrator privileges are not required.

This is an unsigned community executable, so Windows SmartScreen may show a warning. Verify the supplied SHA-256 checksum or build from source if preferred.
