# Installs the H3 Overlay into VCMI. Run through install.cmd (double-click).
#
#  - finds VCMI and your Heroes III data (registry, VCMI's folders, common
#    places, else it asks you to pick the folder)
#  - backs up everything it changes (VCMI settings, key bindings, mod list)
#  - copies the overlay, builds its art/catalog from YOUR game files, installs
#    the small VCMI mod and adds the pad keys to VCMI's key bindings
#  - uninstall.cmd puts the backups back
#
# Parameters are for testing; normally run without any.
param(
    [string]$Vcmi,                 # folder with VCMI_launcher.exe
    [string]$Data,                 # Heroes III Data folder (H3bitmap.lod)
    [string]$UserDir,              # VCMI user folder (default: Documents\My Games\vcmi)
    [string]$Exe,                  # overlay exe to install (default: next to this script or in dist\)
    [switch]$NoShortcut,
    [switch]$NoRegistry,
    [switch]$NonInteractive,
    [string[]]$SearchRoots         # replace the built-in search places
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here 'lib\locate.ps1')
. (Join-Path $here 'lib\backup.ps1')
$utf8 = New-Object Text.UTF8Encoding $false

# --- find things -------------------------------------------------------------
if (-not $UserDir) { $UserDir = Get-VcmiUserDir }
$loc = @{ NoRegistry = $NoRegistry; NonInteractive = $NonInteractive }
if (-not $Vcmi) { $Vcmi = Find-VcmiDir -SearchRoots $SearchRoots @loc }
if (-not (Test-VcmiDir $Vcmi)) { throw "Not a VCMI folder (no VCMI_launcher.exe): $Vcmi" }
if (-not $Data) { $Data = Find-DataDir -SearchRoots $SearchRoots -VcmiDir $Vcmi -UserDir $UserDir @loc }
else { $Data = Resolve-DataDir $Data; if (-not $Data) { throw 'Heroes III Data folder (with H3bitmap.lod) not found in -Data.' } }
if (-not $Exe) {
    foreach ($c in (Join-Path $here 'H3Overlay.exe'), (Join-Path $here 'dist\H3Overlay.exe')) { if (Test-Path -LiteralPath $c) { $Exe = $c; break } }
}
if (-not $Exe -or -not (Test-Path -LiteralPath $Exe)) {
    . (Join-Path $here 'lib\find-zig.ps1')
    if (-not (Test-Path -LiteralPath (Join-Path $here 'build.ps1'))) { throw 'H3Overlay.exe is missing from this folder.' }
    Write-Host 'No built overlay found, building it...'
    & (Join-Path $here 'build.ps1')
    $Exe = Join-Path $here 'dist\H3Overlay.exe'
}
Write-Host "VCMI:        $Vcmi"
Write-Host "Heroes III:  $Data"
Write-Host "VCMI user:   $UserDir"

$state = Join-Path $Vcmi 'H3Overlay'
New-Item -ItemType Directory -Force $state | Out-Null
Open-Manifest $state
Register-Folder $state     # removed again by uninstall (assets, backups, reports)

# Stop a running overlay so its exe can be replaced.
Get-Process -Name 'H3Overlay' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $Vcmi 'H3Overlay.exe') } | Stop-Process -Force

# --- overlay + assets --------------------------------------------------------
Copy-Managed $Exe (Join-Path $Vcmi 'H3Overlay.exe')
Write-Host 'Overlay installed.'

# Panel art and name tables are read from your own game files; nothing from
# the game is shipped with this mod.
& (Join-Path $here 'extract_assets.ps1') -Data $Data -Out $state
Copy-Item -LiteralPath (Join-Path $here 'build_catalog.ps1') -Destination $state -Force
$catalog = Join-Path $Vcmi 'H3Overlay_catalog.txt'
Register-Created $catalog
try {
    & (Join-Path $state 'build_catalog.ps1') -Vcmi $Vcmi -UserDir $UserDir -Assets $state -Out $catalog
} catch {
    Write-Warning "Could not build the creature/skill/artifact catalog: $($_.Exception.Message)"
    Write-Warning 'The overlay still works, but its Army/Skills/Artifacts lists will be empty. Use "Rescan mods" in the panel after starting VCMI once.'
}

# --- VCMI settings-only mod --------------------------------------------------
$modDst = Join-Path $UserDir 'Mods\h3-overlay'
Register-Folder $modDst
New-Item -ItemType Directory -Force $modDst | Out-Null
Copy-Item -LiteralPath (Join-Path $here 'vcmi-mod\h3-overlay\mod.json') -Destination $modDst -Force
$ms = Join-Path $UserDir 'config\modSettings.json'
if (Test-Path -LiteralPath $ms) {
    $cfg = Get-Content -LiteralPath $ms -Raw | ConvertFrom-Json
    $preset = $cfg.presets.($cfg.activePreset)
    if ($preset -and @($preset.mods) -notcontains 'h3-overlay') {
        Protect-File $ms | Out-Null
        $preset.mods = @($preset.mods) + 'h3-overlay'
        Write-Managed $ms ($cfg | ConvertTo-Json -Depth 32)
        Write-Host "Enabled the mod in VCMI preset '$($cfg.activePreset)'."
    }
} else {
    Write-Host 'VCMI has not been run yet (no modSettings.json): start VCMI once, then enable "H3 Overlay" in its Mods tab.'
}

# --- pad keys ----------------------------------------------------------------
# The overlay reads the controller and sends spare keys (F13-F24 and a few unused
# keypad keys), so VCMI's own controller support is turned off and those keys are
# added to its keyboard bindings (vcmi-pad-keys.json). Both copies of
# keyBindingsConfig.json are rebuilt from the saved original every time.
$padKeys = (Get-Content -LiteralPath (Join-Path $here 'vcmi-pad-keys.json') -Raw | ConvertFrom-Json).keys

$kb = Join-Path $Vcmi 'config\keyBindingsConfig.json'
if (Test-Path -LiteralPath $kb) {
    $orig = Protect-File $kb
    $origText = if ($orig) { [IO.File]::ReadAllText($orig) } else { [IO.File]::ReadAllText($kb) }
    $at = $origText.IndexOf('"joystickAxes"')
    if ($at -lt 0) { throw "joystickAxes block not found in $kb" }
    $keyboard = $origText.Substring(0, $at)
    foreach ($k in $padKeys.PSObject.Properties) {
        foreach ($shortcut in $k.Value) {
            $rx = [regex]('("' + [regex]::Escape($shortcut) + '"\s*:\s*)(\[[^\]]*\]|"[^"]*")')
            $m = $rx.Match($keyboard)
            if (-not $m.Success) { throw "Shortcut '$shortcut' not found in $kb" }
            $existing = [regex]::Matches($m.Groups[2].Value, '"([^"]*)"') | ForEach-Object { $_.Groups[1].Value }
            $all = @($existing) + $k.Name | Select-Object -Unique
            $value = '[ ' + (($all | ForEach-Object { '"' + $_ + '"' }) -join ', ') + ' ]'
            $keyboard = $keyboard.Substring(0, $m.Groups[2].Index) + $value + $keyboard.Substring($m.Groups[2].Index + $m.Groups[2].Length)
        }
    }
    $tail = "`"joystickAxes`": {`n`t`t// H3 Overlay: VCMI's controller support is off (settings.json`n`t`t// input.enableController); the overlay reads the pad and sends keys.`n`t},`n`t`"joystickButtons`": {`n`t}`n}`n"
    Write-Managed $kb ($keyboard + $tail)
    Write-Host "Pad keys added to $kb"
} else {
    Write-Host "Note: $kb not found, pad keys not added to the install folder."
}

# VCMI also keeps a copy of the bindings in the user folder when hotkeys are saved,
# and that copy overrides the install folder's. Patch it too if it exists.
$ukb = Join-Path $UserDir 'config\keyBindingsConfig.json'
if (Test-Path -LiteralPath $ukb) {
    $orig = Protect-File $ukb
    $u = Get-Content -LiteralPath $(if ($orig) { $orig } else { $ukb }) -Raw | ConvertFrom-Json
    if ($u.PSObject.Properties['keyboard']) {
        foreach ($k in $padKeys.PSObject.Properties) {
            foreach ($shortcut in $k.Value) {
                $cur = if ($u.keyboard.PSObject.Properties[$shortcut]) { @($u.keyboard.$shortcut) } else { @() }
                $all = @(@($cur) + $k.Name | Where-Object { $_ } | Select-Object -Unique)
                $u.keyboard | Add-Member -NotePropertyName $shortcut -NotePropertyValue $all -Force
            }
        }
    }
    $u | Add-Member -NotePropertyName joystickAxes -NotePropertyValue ([pscustomobject]@{}) -Force
    $u | Add-Member -NotePropertyName joystickButtons -NotePropertyValue ([pscustomobject]@{}) -Force
    Write-Managed $ukb ($u | ConvertTo-Json -Depth 32)
    Write-Host "Pad keys added to $ukb"
}

$st = Join-Path $UserDir 'config\settings.json'
if (Test-Path -LiteralPath $st) {
    $s = Get-Content -LiteralPath $st -Raw | ConvertFrom-Json
    if (-not $s.PSObject.Properties['input']) { $s | Add-Member -NotePropertyName input -NotePropertyValue ([pscustomobject]@{}) }
    if ($s.input.PSObject.Properties['enableController'] -and $s.input.enableController -eq $false) {
        Write-Host 'VCMI controller support already off.'
    } else {
        Protect-File $st | Out-Null
        $s.input | Add-Member -NotePropertyName enableController -NotePropertyValue $false -Force
        Write-Managed $st ($s | ConvertTo-Json -Depth 32)
        Write-Host 'VCMI controller support turned off (the overlay handles the pad).'
    }
}

# --- shortcut ----------------------------------------------------------------
if (-not $NoShortcut) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    if ($desktop -and (Test-Path -LiteralPath $desktop)) {
        $lnk = Join-Path $desktop 'VCMI Heroes 3 (H3 Overlay).lnk'
        Protect-File $lnk | Out-Null
        $w = New-Object -ComObject WScript.Shell
        $sc = $w.CreateShortcut($lnk)
        $sc.TargetPath = Join-Path $Vcmi 'H3Overlay.exe'
        $sc.WorkingDirectory = $Vcmi
        $sc.IconLocation = (Join-Path $Vcmi 'VCMI_launcher.exe') + ',0'
        $sc.Save()
        Write-Host "Desktop shortcut: $lnk"
    }
}

Write-Host ''
Write-Host 'Done. Start Heroes 3 from the "VCMI Heroes 3 (H3 Overlay)" desktop shortcut.'
Write-Host 'In a game: F2 or hold L3 opens the cheat panel. RT+Back reports a bug. To undo everything, run uninstall.cmd.'
