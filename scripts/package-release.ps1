param(
    [string]$Version = '0.1.0'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$binary = Join-Path $projectRoot 'bin\CodexQuotaOrb.exe'
$distRoot = Join-Path $projectRoot 'dist'
$stageRoot = Join-Path $distRoot "CodexQuotaOrb-v$Version-win-x64"
$archive = "$stageRoot.zip"

if (-not (Test-Path -LiteralPath $binary)) {
    throw "Built executable was not found: $binary"
}

$resolvedDist = [IO.Path]::GetFullPath($distRoot)
$expectedParent = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
if (-not $resolvedDist.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace an unexpected directory: $resolvedDist"
}

if (Test-Path -LiteralPath $distRoot) {
    Remove-Item -LiteralPath $distRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path `
    (Join-Path $stageRoot 'bin'), `
    (Join-Path $stageRoot 'docs'), `
    (Join-Path $stageRoot 'scripts') | Out-Null

Copy-Item -LiteralPath $binary -Destination (Join-Path $stageRoot 'bin\CodexQuotaOrb.exe')
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts\install.ps1') -Destination (Join-Path $stageRoot 'scripts\install.ps1')
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts\uninstall.ps1') -Destination (Join-Path $stageRoot 'scripts\uninstall.ps1')
Copy-Item -LiteralPath (Join-Path $projectRoot 'install.cmd') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'uninstall.cmd') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\screenshot.png') -Destination (Join-Path $stageRoot 'docs\screenshot.png')

$binaryHash = Get-FileHash -LiteralPath (Join-Path $stageRoot 'bin\CodexQuotaOrb.exe') -Algorithm SHA256
Set-Content -LiteralPath (Join-Path $stageRoot 'SHA256SUMS.txt') `
    -Value "$($binaryHash.Hash.ToLowerInvariant())  bin/CodexQuotaOrb.exe" `
    -Encoding ascii

Compress-Archive -LiteralPath $stageRoot -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
Set-Content -LiteralPath (Join-Path $distRoot 'SHA256SUMS.txt') `
    -Value "$($archiveHash.Hash.ToLowerInvariant())  $(Split-Path -Leaf $archive)" `
    -Encoding ascii

Write-Host "Release package: $archive"
Write-Host "Archive SHA-256: $($archiveHash.Hash)"
