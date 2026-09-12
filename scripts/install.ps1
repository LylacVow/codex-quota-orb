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
$settingsSubKey = 'Software\CodexQuotaOrb'
$runRegistry = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($runSubKey, $true)
if (-not $runRegistry) {
    throw 'The current-user Run registry key could not be opened for writing.'
}
$firstInstall = -not (Test-Path -LiteralPath $destination) -or $null -eq $runRegistry.GetValue('CodexQuotaOrb', $null)

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
$runRegistry.SetValue('CodexQuotaOrb', ('"{0}" --watch' -f $destination), [Microsoft.Win32.RegistryValueKind]::String)
$runRegistry.Close()
if ($firstInstall) {
    $settingsRegistry = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($settingsSubKey)
    $settingsRegistry.SetValue('HasPosition', 0, [Microsoft.Win32.RegistryValueKind]::DWord)
    $settingsRegistry.Close()
}

if (-not $NoStart) {
    Start-Process -FilePath $destination -ArgumentList '--watch'
}

Write-Host "Codex Quota Orb installed to: $destination"
Write-Host 'It waits at low overhead after sign-in and appears only while Codex is running.'
