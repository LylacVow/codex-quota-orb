$ErrorActionPreference = 'Stop'

$installRoot = Join-Path $env:LOCALAPPDATA 'CodexQuotaOrb'
$expectedParent = [IO.Path]::GetFullPath($env:LOCALAPPDATA).TrimEnd('\') + '\'
$resolvedTarget = [IO.Path]::GetFullPath($installRoot)
if (-not $resolvedTarget.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to delete an unexpected path: $resolvedTarget"
}

Get-Process -Name 'CodexQuotaOrb' -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'CodexQuotaOrb' -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $resolvedTarget) {
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}

Write-Host 'Codex Quota Orb uninstalled. Window preferences remain in HKCU\Software\CodexQuotaOrb.'
