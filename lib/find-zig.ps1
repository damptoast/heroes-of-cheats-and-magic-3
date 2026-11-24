# Locates zig.exe: $env:ZIG, then PATH, then .tools\ in the repo.
function Find-Zig {
    $root = Split-Path -Parent $PSScriptRoot
    if ($env:ZIG -and (Test-Path -LiteralPath $env:ZIG)) { return $env:ZIG }
    $cmd = Get-Command zig -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $local = Get-ChildItem -Path (Join-Path $root '.tools') -Filter 'zig.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($local) { return $local.FullName }
    throw 'Zig 0.14 not found. Install it from https://ziglang.org/download/ and put zig on PATH, set $env:ZIG, or unpack it into .tools\'
}
