# GitHub publishing checklist

## Recommended repository identity

- **Repository name:** `codex-quota-orb`
- **Visibility:** Public
- **Description:** `Ultra-light native Windows tray indicator for Codex weekly quota. 时钟左侧显示周额度百分比，仅在 Codex 运行时同步。`
- **Website:** leave empty unless a project page is added later

## Recommended GitHub Topics

```text
codex
codex-cli
openai-codex
quota-monitor
usage-monitor
rate-limit
weekly-usage
system-tray
taskbar
tray-icon
desktop-widget
windows
windows-11
win32
cpp
native-app
lightweight
productivity
system-utility
open-source
```

## First upload

Create an empty public repository without adding a README, license, or `.gitignore`, then run:

```powershell
git remote add origin https://github.com/YOUR-NAME/codex-quota-orb.git
git push -u origin main
```

## First release

1. Open **Releases → Draft a new release**.
2. Create tag `v0.2.0`; title it `Codex Quota Orb v0.2.0`.
3. Upload `dist/CodexQuotaOrb-v0.2.0-win-x64.zip` and `dist/SHA256SUMS.txt`.
4. Paste `RELEASE_NOTES.md` into the release description and publish.
5. In a signed-out browser, verify the README image, Release files, license, and Topics.
