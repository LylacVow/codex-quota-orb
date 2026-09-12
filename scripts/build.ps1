$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$outputRoot = Join-Path $projectRoot 'bin'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer was not found. Install the Desktop development with C++ workload.'
}

$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw 'The MSVC x64 toolchain was not found.'
}

$devShell = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Force -Path $buildRoot, $outputRoot | Out-Null

$command = '"{0}" -arch=x64 -host_arch=x64 && cmake -S "{1}" -B "{2}" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build "{2}" --config Release' -f $devShell, $projectRoot, $buildRoot
& "$env:SystemRoot\System32\cmd.exe" /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath (Join-Path $buildRoot 'CodexQuotaOrb.exe') -Destination (Join-Path $outputRoot 'CodexQuotaOrb.exe') -Force
$binary = Get-Item -LiteralPath (Join-Path $outputRoot 'CodexQuotaOrb.exe')
$hash = Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256

Write-Host "Built: $($binary.FullName)"
Write-Host "Size: $([math]::Round($binary.Length / 1KB, 1)) KB"
Write-Host "SHA-256: $($hash.Hash)"
