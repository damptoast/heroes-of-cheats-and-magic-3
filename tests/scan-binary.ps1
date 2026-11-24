# Fails if a built binary contains anything that looks like a user name or a
# local path (ASCII or UTF-16). Usage: scan-binary.ps1 [file ...]
param([string[]]$Files = @((Join-Path $PSScriptRoot '..\dist\H3Overlay.exe')))
# Generic markers plus whoever is running the build: user name, computer name,
# profile and repo folders.
$bad = @('users', 'users/', 'appdata', '.pdb', '/home/', 'f:', 'd:', 'c:',
         $env:USERNAME, $env:COMPUTERNAME, (Split-Path -Leaf (Split-Path -Parent $PSScriptRoot))) |
    Where-Object { $_ -and $_.Length -ge 3 } | ForEach-Object { $_.ToLowerInvariant() } | Select-Object -Unique
$failed = $false
foreach ($f in $Files) {
    $bytes = [IO.File]::ReadAllBytes($f)
    $a = [Text.Encoding]::GetEncoding(28591).GetString($bytes).ToLowerInvariant()
    $u = [Text.Encoding]::Unicode.GetString($bytes).ToLowerInvariant()
    $u2 = [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1 - (($bytes.Length - 1) % 2)).ToLowerInvariant()
    foreach ($p in $bad) {
        foreach ($t in @($a, $u, $u2)) {
            $i = $t.IndexOf($p)
            if ($i -ge 0) { Write-Host "FOUND '$p' in $f"; $failed = $true; break }
        }
    }
}
if ($failed) { throw 'binary scan failed' }
Write-Host "Binary scan clean: $($Files -join ', ')"
