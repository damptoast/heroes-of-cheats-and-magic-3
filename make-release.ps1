# Builds the overlay and packs dist\H3Overlay-<version>.zip: what a player needs
# (installer, scripts, the exe, the VCMI mod, README) and nothing from the game.
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1')

$version = [regex]::Match((Get-Content -LiteralPath 'H3_Report.c' -Raw), '#define H3_VERSION "([^"]+)"').Groups[1].Value
if (-not $version) { throw 'H3_VERSION not found in H3_Report.c' }
$stage = Join-Path 'dist' "stage\H3Overlay-$version"
if (Test-Path 'dist\stage') { Remove-Item 'dist\stage' -Recurse -Force }
New-Item -ItemType Directory -Force $stage, "$stage\lib", "$stage\vcmi-mod\h3-overlay" | Out-Null

Copy-Item 'dist\H3Overlay.exe', 'install.cmd', 'uninstall.cmd', 'install.ps1', 'uninstall.ps1',
    'extract_assets.ps1', 'build_catalog.ps1', 'vcmi-pad-keys.json', 'README.md' -Destination $stage
Copy-Item 'lib\locate.ps1', 'lib\backup.ps1' -Destination "$stage\lib"
Copy-Item 'vcmi-mod\h3-overlay\mod.json' -Destination "$stage\vcmi-mod\h3-overlay"

$zip = "dist\H3Overlay-$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage" -DestinationPath $zip
Remove-Item 'dist\stage' -Recurse -Force
Write-Host "Release: $zip ($((Get-Item $zip).Length) bytes)"
