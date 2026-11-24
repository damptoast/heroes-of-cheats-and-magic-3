# Runs install.ps1 / uninstall.ps1 against a fake VCMI + Heroes III in a temp
# folder (never the real ones): finding, backup, reinstall, update-in-between,
# uninstall. Throws on the first failed check.
param([string]$Temp = (Join-Path ([IO.Path]::GetTempPath()) ("h3o-inst-" + [guid]::NewGuid().ToString('N'))))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'lib\locate.ps1')
$utf8 = New-Object Text.UTF8Encoding $false

function Check([bool]$ok, [string]$what) {
    if (-not $ok) { throw "installer test failed: $what" }
    Write-Host "  ok  $what"
}
function Hash([string]$p) { if (Test-Path -LiteralPath $p) { (Get-FileHash -LiteralPath $p).Hash } else { 'missing' } }

# ---- fake world ---------------------------------------------------------------
$w = Join-Path $Temp 'world'
$vcmi = Join-Path $w 'Games\VCMI'
$decoy = Join-Path $w 'Games\NotVCMI'
$user = Join-Path $w 'Docs\My Games\vcmi'
$game = Join-Path $w 'Games\HoMM 3 Complete'
New-Item -ItemType Directory -Force "$vcmi\config", $decoy, "$user\config", "$game\Data" | Out-Null
[IO.File]::WriteAllText("$vcmi\VCMI_launcher.exe", 'fake', $utf8)
[IO.File]::WriteAllText("$vcmi\H3Overlay.exe", 'OLD OVERLAY', $utf8)          # something to be backed up
[IO.File]::WriteAllText("$vcmi\unrelated.txt", 'keep me', $utf8)

$keys = (Get-Content -LiteralPath (Join-Path $root 'vcmi-pad-keys.json') -Raw | ConvertFrom-Json).keys
$shortcuts = $keys.PSObject.Properties | ForEach-Object { $_.Value } | Select-Object -Unique
$kbLines = ($shortcuts | ForEach-Object { "`t`t`"$_`" : [ `"Fake`" ]" }) -join ",`n"
$kbInstall = "{`n`t`"keyboard`" : {`n$kbLines`n`t},`n`t`"joystickAxes`": {`n`t`t`"x`": 1`n`t},`n`t`"joystickButtons`": {`n`t`t`"a`": 1`n`t}`n}`n"
[IO.File]::WriteAllText("$vcmi\config\keyBindingsConfig.json", $kbInstall, $utf8)
$ukb = [ordered]@{ keyboard = [ordered]@{}; joystickAxes = @{ x = 1 } }
foreach ($s in $shortcuts) { $ukb.keyboard[$s] = @('UserKey') }
[IO.File]::WriteAllText("$user\config\keyBindingsConfig.json", ($ukb | ConvertTo-Json -Depth 8), $utf8)
[IO.File]::WriteAllText("$user\config\settings.json", '{"general":{"language":"english"},"input":{"enableController":true}}', $utf8)
[IO.File]::WriteAllText("$user\config\modSettings.json", '{"activePreset":"default","presets":{"default":{"mods":["vcmi"]}}}', $utf8)

# A minimal LOD with the files the extractor wants (stored, not packed).
function New-FakeLod([string]$path) {
    $names = 'DIBOXBCK.PCX', 'BIGFONT.FNT', 'MEDFONT.FNT', 'SMALFONT.FNT', 'CRTRAITS.TXT', 'SSTRAITS.TXT', 'ARTRAITS.TXT'
    $pcx = New-Object System.Collections.Generic.List[byte]
    $pcx.AddRange([BitConverter]::GetBytes([uint32]4)); $pcx.AddRange([BitConverter]::GetBytes([int]2)); $pcx.AddRange([BitConverter]::GetBytes([int]2))
    $pcx.AddRange([byte[]](0, 1, 2, 3)); $pcx.AddRange([byte[]](,[byte]7 * 768))
    $bodies = @{}
    foreach ($n in $names) { $bodies[$n] = [byte[]]$(if ($n -eq "DIBOXBCK.PCX") { $pcx.ToArray() } else { $utf8.GetBytes("fake $n") }) }
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([byte[]](0x4C, 0x4F, 0x44, 0)); $bw.Write([uint32]200); $bw.Write([uint32]$names.Count)
    $bw.Write((New-Object byte[] 80))
    $offset = 92 + 32 * $names.Count
    foreach ($n in $names) {
        $nb = New-Object byte[] 16; [Text.Encoding]::ASCII.GetBytes($n).CopyTo($nb, 0); $bw.Write($nb)
        $bw.Write([uint32]$offset); $bw.Write([uint32]$bodies[$n].Length); $bw.Write([uint32]0); $bw.Write([uint32]0)
        $offset += $bodies[$n].Length
    }
    foreach ($n in $names) { $bw.Write($bodies[$n]) }
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}
New-FakeLod "$game\Data\H3bitmap.lod"
[IO.File]::WriteAllText("$Temp\fake-overlay.exe", 'NEW OVERLAY', $utf8)

$tracked = @("$vcmi\H3Overlay.exe", "$vcmi\config\keyBindingsConfig.json", "$user\config\keyBindingsConfig.json",
             "$user\config\settings.json", "$user\config\modSettings.json", "$vcmi\unrelated.txt")
$before = @{}; foreach ($t in $tracked) { $before[$t] = Hash $t }

try {
    # ---- finding ------------------------------------------------------------------
    Write-Host 'find:'
    $found = Find-VcmiDir -SearchRoots @($decoy, $vcmi) -NoRegistry -NonInteractive
    Check ($found -eq (Resolve-Path $vcmi).ProviderPath) 'VCMI found, folder without launcher skipped'
    $thrown = $false; try { Find-VcmiDir -SearchRoots @($decoy) -NoRegistry -NonInteractive } catch { $thrown = $true }
    Check $thrown 'no VCMI -> error (instead of the picker when non-interactive)'
    $d1 = Find-DataDir -SearchRoots @($game) -NoRegistry -NonInteractive
    $d2 = Find-DataDir -SearchRoots @("$game\Data") -NoRegistry -NonInteractive
    Check ($d1 -eq $d2 -and (Test-DataDir $d1)) 'Heroes III data found from game folder or Data folder'
    $thrown = $false; try { Find-DataDir -SearchRoots @($decoy) -NoRegistry -NonInteractive } catch { $thrown = $true }
    Check $thrown 'no data -> error'

    # ---- install ------------------------------------------------------------------
    $common = @{ Vcmi = $vcmi; UserDir = $user; Exe = "$Temp\fake-overlay.exe"; NoShortcut = $true; NonInteractive = $true }
    Write-Host 'install:'
    $out = & (Join-Path $root 'install.ps1') @common -SearchRoots @($game) -NoRegistry 6>&1 | Out-String
    Check ((Get-Content "$vcmi\H3Overlay.exe" -Raw) -eq 'NEW OVERLAY') 'overlay exe installed over the old one'
    Check (Test-Path "$vcmi\H3Overlay\diboxbck.bmp") 'panel art extracted from the game data'
    Check (Test-Path "$vcmi\H3Overlay\BIGFONT.FNT") 'fonts extracted'
    Check (Test-Path "$user\Mods\h3-overlay\mod.json") 'VCMI mod copied'
    Check ((Get-Content "$user\config\modSettings.json" -Raw) -match 'h3-overlay') 'mod enabled in the active preset'
    Check ((Get-Content "$user\config\settings.json" -Raw | ConvertFrom-Json).input.enableController -eq $false) 'VCMI controller support off'
    $kb = Get-Content "$vcmi\config\keyBindingsConfig.json" -Raw
    Check ($kb -match '"globalCancel"\s*:\s*\[[^\]]*"F13"') 'pad key F13 added to install-folder bindings'
    Check ($kb -match '"Fake"') 'existing bindings kept'
    Check ($kb -notmatch '"x"\s*:\s*1' -and $kb -notmatch '"a"\s*:\s*1') 'joystick bindings emptied'
    $ukbNow = Get-Content "$user\config\keyBindingsConfig.json" -Raw | ConvertFrom-Json
    Check ((@($ukbNow.keyboard.globalCancel) -contains 'F13') -and (@($ukbNow.keyboard.globalCancel) -contains 'UserKey')) 'pad key added to the user-folder bindings'
    Check (Test-Path "$vcmi\H3Overlay\install-manifest.json") 'manifest written'
    Check ((Get-Content "$vcmi\H3Overlay\backup\*H3Overlay.exe" -Raw) -eq 'OLD OVERLAY') 'old exe backed up'

    # ---- reinstall is idempotent and keeps the originals ----------------------------
    Write-Host 'reinstall:'
    $afterFirst = @{}; foreach ($t in $tracked) { $afterFirst[$t] = Hash $t }
    & (Join-Path $root 'install.ps1') @common -SearchRoots @($game) -NoRegistry 6>&1 | Out-Null
    foreach ($t in $tracked) { Check ((Hash $t) -eq $afterFirst[$t]) "unchanged by second install: $(Split-Path -Leaf $t)" }

    # ---- VCMI updates its own file between installs -------------------------------
    Write-Host 'VCMI rewrites its bindings, then reinstall:'
    $updated = $kbInstall.Replace('"Fake"', '"Fake2"')
    [IO.File]::WriteAllText("$vcmi\config\keyBindingsConfig.json", $updated, $utf8)
    $before["$vcmi\config\keyBindingsConfig.json"] = Hash "$vcmi\config\keyBindingsConfig.json"
    & (Join-Path $root 'install.ps1') @common -SearchRoots @($game) -NoRegistry 6>&1 | Out-Null
    $kb = Get-Content "$vcmi\config\keyBindingsConfig.json" -Raw
    Check ($kb -match '"Fake2"' -and $kb -match '"F13"') 'new VCMI file patched, not the stale backup'

    # ---- uninstall ------------------------------------------------------------------
    Write-Host 'uninstall:'
    & (Join-Path $root 'uninstall.ps1') -Vcmi $vcmi 6>&1 | Out-Null
    foreach ($t in $tracked) { Check ((Hash $t) -eq $before[$t]) "restored: $(Split-Path -Leaf $t)" }
    Check (-not (Test-Path "$vcmi\H3Overlay")) 'overlay folder (assets, backups, manifest) removed'
    Check (-not (Test-Path "$user\Mods\h3-overlay")) 'VCMI mod removed'
    Check (-not (Test-Path "$vcmi\H3Overlay_catalog.txt")) 'catalog removed'
    Write-Host 'installer tests passed'
} finally {
    Remove-Item -LiteralPath $w -Recurse -Force -ErrorAction SilentlyContinue
}
