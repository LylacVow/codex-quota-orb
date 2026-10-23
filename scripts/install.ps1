param(
    [switch]$NoStart
)

$ErrorActionPreference = 'Stop'

$source = Join-Path (Split-Path -Parent $PSScriptRoot) 'bin\CodexQuotaOrb.exe'
if (-not (Test-Path -LiteralPath $source)) {
    throw "Built executable was not found: $source"
}

$installRoot = Join-Path $env:LOCALAPPDATA 'CodexQuotaOrb'
$destination = Join-Path $installRoot 'CodexQuotaOrb.exe'
$runSubKey = 'Software\Microsoft\Windows\CurrentVersion\Run'
$runRegistry = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($runSubKey, $true)
if (-not $runRegistry) {
    throw 'The current-user Run registry key could not be opened for writing.'
}
$running = @(Get-Process -Name 'CodexQuotaOrb' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    $running | Stop-Process -Force
    $running | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
$copied = $false
for ($attempt = 0; $attempt -lt 10 -and -not $copied; $attempt++) {
    try {
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $copied = $true
    } catch [System.IO.IOException] {
        if ($attempt -eq 9) { throw }
        Start-Sleep -Milliseconds 200
    }
}
$runRegistry.SetValue('CodexQuotaOrb', ('"{0}"' -f $destination), [Microsoft.Win32.RegistryValueKind]::String)
$runRegistry.Close()

if (-not $NoStart) {
    Start-Process -FilePath $destination -WindowStyle Hidden

    # Ask Windows 11 to keep this stable GUID in the visible notification area.
    # Explorer may apply the preference on its next session; users can always
    # drag the icon beside the clock once to apply it immediately.
    $notifyRoot = 'HKCU:\Control Panel\NotifyIconSettings'
    $trayGuid = '{7A799DAA-562A-4DF7-9199-1F36B84D7029}'
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        $entry = Get-ChildItem -LiteralPath $notifyRoot -ErrorAction SilentlyContinue |
            Where-Object {
                $properties = Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue
                $properties.IconGuid -eq $trayGuid
            } |
            Select-Object -First 1
        if ($entry) {
            New-ItemProperty -LiteralPath $entry.PSPath -Name 'IsPromoted' `
                -PropertyType DWord -Value 1 -Force | Out-Null
            break
        }
        Start-Sleep -Milliseconds 100
    }
}

Write-Host "Codex Quota Orb installed to: $destination"
Write-Host 'The low-overhead tray indicator stays resident and syncs only while Codex is running.'
