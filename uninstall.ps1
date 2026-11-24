# Removes the H3 Overlay and puts back every file the installer changed, from
# the backups it made. Run through uninstall.cmd (double-click).
param(
    [string]$Vcmi,
    [switch]$NoRegistry,
    [switch]$NonInteractive,
    [string[]]$SearchRoots
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\locate.ps1')
. (Join-Path $PSScriptRoot 'lib\backup.ps1')

if (-not $Vcmi) { $Vcmi = Find-VcmiDir -SearchRoots $SearchRoots -NoRegistry:$NoRegistry -NonInteractive:$NonInteractive }
$state = Join-Path $Vcmi 'H3Overlay'
if (-not (Test-Path -LiteralPath (Join-Path $state 'install-manifest.json'))) {
    throw "The H3 Overlay is not installed in $Vcmi (no install-manifest.json)."
}
Get-Process -Name 'H3Overlay' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $Vcmi 'H3Overlay.exe') } | Stop-Process -Force
Start-Sleep -Milliseconds 300

$log = Restore-All $state
$log | ForEach-Object { Write-Host $_ }
# Files the overlay made on its own while running.
foreach ($f in 'H3Overlay.ini', 'H3Overlay.log') {
    $p = Join-Path $Vcmi $f
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force; Write-Host "removed $p" }
}
Write-Host ''
Write-Host 'Uninstalled. VCMI settings, key bindings and mod list are back as they were.'
