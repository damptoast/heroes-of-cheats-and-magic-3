# Finds VCMI and the Heroes III data without any hard-coded user paths:
# registry (uninstall entries, GOG), VCMI's own folders, common install
# locations on every fixed drive, and finally a folder picker.
# -SearchRoots replaces the built-in candidate folders (used by the tests);
# -NoRegistry skips registry lookups; -NonInteractive throws instead of asking.

function Get-FixedDrives {
    [IO.DriveInfo]::GetDrives() | Where-Object { $_.DriveType -eq 'Fixed' -and $_.IsReady } | ForEach-Object { $_.Name }
}

function Test-VcmiDir([string]$dir) {
    $dir -and (Test-Path -LiteralPath $dir -PathType Container) -and
        ((Test-Path -LiteralPath (Join-Path $dir 'VCMI_launcher.exe')) -or (Test-Path -LiteralPath (Join-Path $dir 'VCMI_client.exe')))
}

function Test-DataDir([string]$dir) {
    $dir -and (Test-Path -LiteralPath (Join-Path $dir 'H3bitmap.lod'))
}

# Accepts the game folder or its Data folder; returns the Data folder or $null.
function Resolve-DataDir([string]$dir) {
    if (-not $dir) { return $null }
    if (Test-DataDir $dir) { return $dir }
    $d = Join-Path $dir 'Data'
    if (Test-DataDir $d) { return $d }
    return $null
}

function Get-VcmiUserDir {
    Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\vcmi'
}

function Select-Folder([string]$description) {
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object Windows.Forms.FolderBrowserDialog
    $dlg.Description = $description
    $dlg.ShowNewFolderButton = $false
    if ($dlg.ShowDialog() -eq 'OK') { return $dlg.SelectedPath }
    return $null
}

function Find-VcmiDir {
    param([string[]]$SearchRoots, [switch]$NoRegistry, [switch]$NonInteractive)
    $cands = New-Object Collections.Generic.List[string]
    if (-not $NoRegistry) {
        foreach ($hive in 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall') {
            Get-ChildItem $hive -ErrorAction SilentlyContinue | ForEach-Object {
                $p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
                if ($p.DisplayName -like 'VCMI*') {
                    if ($p.InstallLocation) { $cands.Add(([string]$p.InstallLocation).Trim('"')) }
                    foreach ($f in $p.DisplayIcon, $p.UninstallString) {
                        if ($f) { $cands.Add((Split-Path -Parent (([string]$f -replace ',\d+$', '').Trim('"')))) }
                    }
                }
            }
        }
    }
    if ($SearchRoots) {
        $SearchRoots | ForEach-Object { $cands.Add($_) }
    } else {
        foreach ($base in $env:ProgramFiles, ${env:ProgramFiles(x86)}, (Join-Path $env:LOCALAPPDATA 'Programs')) {
            if ($base) { $cands.Add((Join-Path $base 'VCMI')) }
        }
        foreach ($drive in Get-FixedDrives) {
            foreach ($rel in 'VCMI', 'Games\VCMI', 'Program Files\VCMI', 'Program Files (x86)\VCMI') { $cands.Add((Join-Path $drive $rel)) }
        }
    }
    foreach ($c in $cands) { if (Test-VcmiDir $c) { return (Resolve-Path -LiteralPath $c).ProviderPath } }
    if ($NonInteractive) { throw 'VCMI not found.' }
    while ($true) {
        $pick = Select-Folder 'Select the folder where VCMI is installed (it contains VCMI_launcher.exe).'
        if (-not $pick) { throw 'No VCMI folder selected.' }
        if (Test-VcmiDir $pick) { return $pick }
        Write-Host "No VCMI_launcher.exe in $pick, try again."
    }
}

function Find-DataDir {
    param([string[]]$SearchRoots, [string]$VcmiDir, [string]$UserDir, [switch]$NoRegistry, [switch]$NonInteractive)
    $cands = New-Object Collections.Generic.List[string]
    # VCMI's own data folders first: that is what VCMI itself reads.
    if ($UserDir) { $cands.Add((Join-Path $UserDir 'Data')) }
    if ($VcmiDir) { $cands.Add((Join-Path $VcmiDir 'Data')) }
    if (-not $NoRegistry) {
        foreach ($root in 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games', 'HKLM:\SOFTWARE\GOG.com\Games') {
            Get-ChildItem $root -ErrorAction SilentlyContinue | ForEach-Object {
                $p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
                if ($p.path) { $cands.Add([string]$p.path) }
            }
        }
        $reg = [string][char]0xAE
        foreach ($k in "HKLM:\SOFTWARE\WOW6432Node\New World Computing\Heroes of Might and Magic$reg 3\1.0", "HKLM:\SOFTWARE\New World Computing\Heroes of Might and Magic$reg 3\1.0") {
            $p = Get-ItemProperty -LiteralPath $k -ErrorAction SilentlyContinue
            if ($p.AppPath) { $cands.Add([string]$p.AppPath) }
        }
    }
    if ($SearchRoots) {
        $SearchRoots | ForEach-Object { $cands.Add($_) }
    } else {
        $names = 'HoMM 3 Complete', 'Heroes of Might and Magic 3 Complete', 'Heroes of Might and Magic III', 'HoMM3', 'Heroes3'
        $bases = New-Object Collections.Generic.List[string]
        foreach ($b in ${env:ProgramFiles(x86)}, $env:ProgramFiles) {
            if ($b) { $bases.Add((Join-Path $b 'GOG Galaxy\Games')); $bases.Add((Join-Path $b 'GOG Games')); $bases.Add($b) }
        }
        foreach ($drive in Get-FixedDrives) {
            foreach ($rel in 'Games', 'GOG Games', 'GOG Galaxy\Games', '') { $bases.Add((Join-Path $drive $rel)) }
        }
        foreach ($b in $bases) { foreach ($n in $names) { $cands.Add((Join-Path $b $n)) } }
    }
    foreach ($c in $cands) {
        $d = Resolve-DataDir $c
        if ($d) { return (Resolve-Path -LiteralPath $d).ProviderPath }
    }
    if ($NonInteractive) { throw 'Heroes III data not found.' }
    while ($true) {
        $pick = Select-Folder 'Select your Heroes of Might and Magic III folder (the one with a Data folder containing H3bitmap.lod).'
        if (-not $pick) { throw 'No Heroes III folder selected.' }
        $d = Resolve-DataDir $pick
        if ($d) { return $d }
        Write-Host "No Data\H3bitmap.lod in $pick, try again."
    }
}
