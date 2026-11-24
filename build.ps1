# Builds dist\H3Overlay.exe (64-bit overlay for VCMI) with Zig's C compiler.
# Zig is taken from $env:ZIG, then PATH, then .tools\zig-*\ in this folder.
# Everything is relative to this script, and nothing machine-specific is
# written into the binary (no debug info, no build timestamp, no source paths).
param([switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

. (Join-Path $PSScriptRoot 'lib\find-zig.ps1')
$zig = Find-Zig

New-Item -ItemType Directory -Force 'dist' | Out-Null
$out = 'dist\H3Overlay.exe'
Write-Host "Building $out"
# -s strips symbols, -ffile-prefix-map keeps __FILE__ relative.
& $zig cc -target x86_64-windows-gnu -O2 -static -Wall -s '-ffile-prefix-map=.=.' `
    -o $out 'H3Overlay.c' -luser32 -lkernel32 -lshell32 -lgdi32 -lgdiplus -lversion `
    '-Wl,--subsystem,windows'
if ($LASTEXITCODE -ne 0) { throw "zig cc failed: $LASTEXITCODE" }
Get-ChildItem dist -Include *.pdb, *.lib -Recurse | Remove-Item -Force
Write-Host "OK $out ($((Get-Item $out).Length) bytes)"

if ($Test) { & (Join-Path $PSScriptRoot 'tests\run-tests.ps1') }
