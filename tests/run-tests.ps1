# Runs every test: the C report helpers, the installer against a fake
# VCMI/game folder in a temp dir, and the binary scan (if dist\ is built).
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

. (Join-Path $root 'lib\find-zig.ps1')
$zig = Find-Zig
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("h3o-tests-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    $exe = Join-Path $tmp 'test_report.exe'
    & $zig cc -target x86_64-windows-gnu -O1 -Wall -o $exe 'tests\test_report.c'
    if ($LASTEXITCODE -ne 0) { throw 'compiling test_report.c failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'report tests failed' }

    & (Join-Path $PSScriptRoot 'test-installer.ps1') -Temp $tmp

    if (Test-Path 'dist\H3Overlay.exe') { & (Join-Path $PSScriptRoot 'scan-binary.ps1') }
    else { Write-Host 'dist\H3Overlay.exe not built, binary scan skipped' }
    Write-Host 'ALL TESTS PASSED'
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
