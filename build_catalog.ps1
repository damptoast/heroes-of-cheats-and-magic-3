# Builds H3Overlay_catalog.txt: every creature, secondary skill and hero artifact
# the cheat panel can give, from the base game plus every enabled VCMI mod.
# Each entry carries the fully scoped identifier VCMI's cheats resolve
# ("core:pikeman", "hota.cove:corsair"), so same-named objects in two mods
# never make a cheat silently do nothing.
#
# Lines (tab separated, Windows-1252):
#   F <faction key> <faction name>
#   C <faction key> <level> <name> <id> <unique>
#   S <name> <id> <unique>
#   A <class> <name> <id> <unique>
# <unique> is 1 when no other loaded object of that type shares the bare name,
# so the overlay can type the short form ("griffin") and avoid ':' entirely.
param(
    [Parameter(Mandatory)][string]$Vcmi,
    [string]$UserDir = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\vcmi'),
    [string]$Assets = (Join-Path $PSScriptRoot 'assets'),
    [string]$Out = (Join-Path $Vcmi 'H3Overlay_catalog.txt')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# ---- lenient JSON: VCMI files allow comments and trailing commas ----
function ConvertFrom-VcmiJson([string]$text) {
    $sb = New-Object Text.StringBuilder $text.Length
    $i = 0; $n = $text.Length; $inStr = $false
    while ($i -lt $n) {
        $c = $text[$i]
        if ($inStr) {
            [void]$sb.Append($c)
            if ($c -eq '\' -and $i + 1 -lt $n) { [void]$sb.Append($text[$i + 1]); $i += 2; continue }
            if ($c -eq '"') { $inStr = $false }
            $i++; continue
        }
        if ($c -eq '"') { $inStr = $true; [void]$sb.Append($c); $i++; continue }
        if ($c -eq '/' -and $i + 1 -lt $n -and $text[$i + 1] -eq '/') {
            while ($i -lt $n -and $text[$i] -ne "`n") { $i++ }
            continue
        }
        if ($c -eq '/' -and $i + 1 -lt $n -and $text[$i + 1] -eq '*') {
            $end = $text.IndexOf('*/', $i + 2)
            $i = if ($end -lt 0) { $n } else { $end + 2 }
            continue
        }
        [void]$sb.Append($c); $i++
    }
    $clean = [regex]::Replace($sb.ToString(), ',(\s*[}\]])', '$1')
    return $clean | ConvertFrom-Json
}

# ---- Heroes III .TXT tables: tab separated, quoted fields may span lines ----
function Read-H3Table([string]$path) {
    $t = [IO.File]::ReadAllText($path, [Text.Encoding]::GetEncoding(1252))
    $rows = New-Object Collections.Generic.List[object]
    $row = New-Object Collections.Generic.List[string]
    $f = New-Object Text.StringBuilder
    $i = 0; $n = $t.Length; $q = $false
    while ($i -lt $n) {
        $c = $t[$i]
        if ($q) {
            if ($c -eq '"') {
                if ($i + 1 -lt $n -and $t[$i + 1] -eq '"') { [void]$f.Append('"'); $i += 2; continue }
                $q = $false; $i++; continue
            }
            [void]$f.Append($c); $i++; continue
        }
        if ($c -eq '"' -and $f.Length -eq 0) { $q = $true; $i++; continue }
        if ($c -eq "`t") { $row.Add($f.ToString()); [void]$f.Clear(); $i++; continue }
        if ($c -eq "`r") { $i++; continue }
        if ($c -eq "`n") {
            $row.Add($f.ToString()); [void]$f.Clear()
            $rows.Add($row.ToArray()); $row.Clear(); $i++; continue
        }
        [void]$f.Append($c); $i++
    }
    if ($f.Length -gt 0 -or $row.Count -gt 0) { $row.Add($f.ToString()); $rows.Add($row.ToArray()) }
    return , $rows
}

function Get-Prop($obj, [string]$name) {
    if ($null -eq $obj) { return $null }
    $p = $obj.PSObject.Properties[$name]
    if ($p) { return $p.Value }
    return $null
}

# "fireDragonFly" -> "Fire Dragon Fly"
function Get-Pretty([string]$id) {
    $s = [regex]::Replace($id, '([a-z0-9])([A-Z])', '$1 $2')
    $s = $s -replace '[_\-]', ' '
    return (Get-Culture).TextInfo.ToTitleCase($s)
}

function Clean([string]$s) {
    if (-not $s) { return '' }
    return ($s -replace '[\t\r\n]', ' ').Trim()
}

$factions = [ordered]@{}      # key -> name
# Every bare object name seen per type (including ones the panel does not list,
# like commanders or war machines), to tell which short names are unambiguous.
$bare = @{ creature = @{}; skill = @{}; artifact = @{} }
function Note-Bare([string]$type, [string]$name) {
    $k = $name.ToLowerInvariant()
    $bare[$type][$k] = 1 + [int]$bare[$type][$k]
}
$creatures = New-Object Collections.Generic.List[object]
$skills = New-Object Collections.Generic.List[object]
$artifacts = New-Object Collections.Generic.List[object]
$seen = @{}

# ---------------- base game ----------------
$baseFactions = [ordered]@{
    castle = 'Castle'; rampart = 'Rampart'; tower = 'Tower'; inferno = 'Inferno'
    necropolis = 'Necropolis'; dungeon = 'Dungeon'; stronghold = 'Stronghold'
    fortress = 'Fortress'; conflux = 'Conflux'
}
foreach ($k in $baseFactions.Keys) { $factions[$k] = $baseFactions[$k] }

$crNames = Read-H3Table (Join-Path $Assets 'CRTRAITS.TXT')
$j = ConvertFrom-VcmiJson (Get-Content -Raw (Join-Path $Vcmi 'config\creatures\special.json'))
foreach ($p in $j.PSObject.Properties) { Note-Bare creature $p.Name }
foreach ($f in $baseFactions.Keys + @('neutral')) {
    $j = ConvertFrom-VcmiJson (Get-Content -Raw (Join-Path $Vcmi "config\creatures\$f.json"))
    foreach ($p in $j.PSObject.Properties) {
        Note-Bare creature $p.Name
        $idx = Get-Prop $p.Value 'index'
        $lvl = Get-Prop $p.Value 'level'
        if ($null -eq $idx -or -not $lvl) { continue }
        $row = $crNames[2 + [int]$idx]
        $name = if ($row -and $row[0]) { $row[0] } else { Get-Pretty $p.Name }
        $creatures.Add([pscustomobject]@{ Faction = $f; Level = [int]$lvl; Name = $name; Id = "core:$($p.Name)" })
    }
}

$ssNames = Read-H3Table (Join-Path $Assets 'SSTRAITS.TXT')
$j = ConvertFrom-VcmiJson (Get-Content -Raw (Join-Path $Vcmi 'config\skills.json'))
$i = 0
foreach ($p in $j.PSObject.Properties) {
    Note-Bare skill $p.Name
    $idx = Get-Prop $p.Value 'index'
    if ($null -eq $idx) { $idx = $i }
    $row = $ssNames[2 + [int]$idx]
    $name = if ($row -and $row[0]) { $row[0] } else { Get-Pretty $p.Name }
    $skills.Add([pscustomobject]@{ Name = $name; Id = "core:$($p.Name)" })
    $i++
}

$classNames = @{ S = 'Special'; T = 'Treasure'; N = 'Minor'; J = 'Major'; R = 'Relic' }
$arNames = Read-H3Table (Join-Path $Assets 'ARTRAITS.TXT')
$classCol = [Array]::IndexOf($arNames[1], ($arNames[1] | Where-Object { $_ -like 'Class*' } | Select-Object -First 1))
$j = ConvertFrom-VcmiJson (Get-Content -Raw (Join-Path $Vcmi 'config\artifacts.json'))
foreach ($p in $j.PSObject.Properties) {
    Note-Bare artifact $p.Name
    $a = $p.Value
    $idx = Get-Prop $a 'index'
    if ($null -eq $idx -or (Get-Prop $a 'warMachine')) { continue }
    if ($p.Name -in 'spellBook', 'spellScroll') { continue }
    $type = @(Get-Prop $a 'type')
    if ($type.Count -and $type -notcontains 'HERO') { continue }
    $row = $arNames[2 + [int]$idx]
    if (-not $row -or -not $row[0]) { continue }
    $cls = if ($classCol -ge 0 -and $row.Count -gt $classCol) { $classNames[$row[$classCol].Trim()] } else { $null }
    if (-not $cls) { $cls = 'Special' }
    $artifacts.Add([pscustomobject]@{ Class = $cls; Name = $row[0]; Id = "core:$($p.Name)" })
}

# ---------------- mods ----------------
$modCfg = ConvertFrom-VcmiJson (Get-Content -Raw (Join-Path $UserDir 'config\modSettings.json'))
$preset = Get-Prop (Get-Prop $modCfg 'presets') (Get-Prop $modCfg 'activePreset')
$active = @(Get-Prop $preset 'mods')
$settings = Get-Prop $preset 'settings'
$modsRoot = Join-Path $UserDir 'Mods'

# Opens a mod's content (content.zip or a content folder) as a lookup of
# lowercase relative path -> reader.
function Open-Content([string]$modDir) {
    $zip = Get-ChildItem -LiteralPath $modDir -File -Filter 'content.zip' -ErrorAction SilentlyContinue | Select-Object -First 1
    $map = @{}
    if ($zip) {
        $z = [IO.Compression.ZipFile]::OpenRead($zip.FullName)
        foreach ($e in $z.Entries) { $map[$e.FullName.Replace('\', '/').ToLowerInvariant()] = $e }
        return @{ Zip = $z; Map = $map }
    }
    $dir = Get-ChildItem -LiteralPath $modDir -Directory | Where-Object { $_.Name -ieq 'content' } | Select-Object -First 1
    if ($dir) {
        $root = $dir.FullName.TrimEnd('\') + '\'
        Get-ChildItem -LiteralPath $dir.FullName -Recurse -File -Filter *.json | ForEach-Object {
            $map[$_.FullName.Substring($root.Length).Replace('\', '/').ToLowerInvariant()] = $_.FullName
        }
    }
    return @{ Zip = $null; Map = $map }
}

function Read-ContentJson($content, [string]$rel) {
    $key = ($rel.Replace('\', '/').TrimStart('/') + '.json').ToLowerInvariant()
    if ($rel -match '\.json$') { $key = $rel.Replace('\', '/').TrimStart('/').ToLowerInvariant() }
    $e = $content.Map[$key]
    if (-not $e) { return $null }
    if ($e -is [string]) { $text = [IO.File]::ReadAllText($e) }
    else { $r = New-Object IO.StreamReader($e.Open()); $text = $r.ReadToEnd(); $r.Dispose() }
    try { return ConvertFrom-VcmiJson $text } catch { Write-Warning "Skipped $rel ($($_.Exception.Message))"; return $null }
}

function Test-Enabled([string]$top, [string]$scope, $modJson) {
    if ($scope -eq $top) { return $true }
    $sub = $scope.Substring($top.Length + 1)
    $v = Get-Prop (Get-Prop $settings $top) $sub
    if ($null -ne $v) { return [bool]$v }
    return -not [bool](Get-Prop $modJson 'keepDisabled')
}

$modFactionCreatures = New-Object Collections.Generic.List[object]
$script:modNames = New-Object Collections.Generic.List[string]

function Read-Mod([string]$dir, [string]$scope, [string]$top) {
    $mj = Join-Path $dir 'mod.json'
    if (-not (Test-Path -LiteralPath $mj)) { return }
    try { $m = ConvertFrom-VcmiJson (Get-Content -Raw -LiteralPath $mj) } catch { return }
    if (-not (Test-Enabled $top $scope $m)) { return }
    if ($scope -eq $top) {
        $script:modNames.Add(("{0} ({1})" -f (Clean ([string](Get-Prop $m 'name'))), $scope))
    }
    $lists = @{}
    $total = 0
    foreach ($k in 'factions', 'creatures', 'skills', 'artifacts') {
        $lists[$k] = @(@(Get-Prop $m $k) | Where-Object { $_ })
        $total += $lists[$k].Count
    }
    if ($total -gt 0) {
        $content = Open-Content $dir
        try {
            foreach ($rel in $lists['factions']) {
                $j = Read-ContentJson $content $rel
                if (-not $j) { continue }
                foreach ($p in $j.PSObject.Properties) {
                    if ($p.Name -match ':') { continue }
                    $nm = Get-Prop $p.Value 'name'
                    if ($nm -and -not $factions.Contains($p.Name)) { $factions[$p.Name] = Clean $nm }
                }
            }
            foreach ($rel in $lists['creatures']) {
                $j = Read-ContentJson $content $rel
                if (-not $j) { continue }
                foreach ($p in $j.PSObject.Properties) {
                    if ($p.Name -match ':') { continue }
                    if (-not $seen["bare:$scope`:$($p.Name)"]) {
                        $seen["bare:$scope`:$($p.Name)"] = $true
                        Note-Bare creature $p.Name
                    }
                    $c = $p.Value
                    $lvl = Get-Prop $c 'level'
                    if (-not $lvl -or (Get-Prop $c 'special')) { continue }
                    $nm = Get-Prop (Get-Prop $c 'name') 'singular'
                    if (-not $nm) { $nm = Get-Pretty $p.Name }
                    $fac = [string](Get-Prop $c 'faction')
                    if ($fac -match ':') { $fac = $fac.Split(':')[1] }
                    if (-not $fac) { $fac = 'neutral' }
                    $id = "$scope`:$($p.Name)"
                    if ($seen[$id]) { continue }
                    $seen[$id] = $true
                    $modFactionCreatures.Add([pscustomobject]@{ Faction = $fac; Level = [int]$lvl; Name = (Clean $nm); Id = $id })
                }
            }
            foreach ($rel in $lists['skills']) {
                $j = Read-ContentJson $content $rel
                if (-not $j) { continue }
                foreach ($p in $j.PSObject.Properties) {
                    if ($p.Name -match ':') { continue }
                    Note-Bare skill $p.Name
                    $nm = Get-Prop $p.Value 'name'
                    if ($nm -isnot [string]) { $nm = $null }
                    if (-not $nm) { $nm = Get-Pretty $p.Name }
                    $skills.Add([pscustomobject]@{ Name = (Clean $nm); Id = "$scope`:$($p.Name)" })
                }
            }
            foreach ($rel in $lists['artifacts']) {
                $j = Read-ContentJson $content $rel
                if (-not $j) { continue }
                foreach ($p in $j.PSObject.Properties) {
                    if ($p.Name -match ':') { continue }
                    Note-Bare artifact $p.Name
                    $a = $p.Value
                    if (Get-Prop $a 'warMachine') { continue }
                    $type = @(Get-Prop $a 'type')
                    if ($type.Count -and $type -notcontains 'HERO') { continue }
                    $nm = Get-Prop (Get-Prop $a 'text') 'name'
                    if (-not $nm) { $nm = Get-Pretty $p.Name }
                    $cls = [string](Get-Prop $a 'class')
                    $cls = if ($cls) { (Get-Culture).TextInfo.ToTitleCase($cls.ToLowerInvariant()) } else { 'Special' }
                    $artifacts.Add([pscustomobject]@{ Class = $cls; Name = (Clean $nm); Id = "$scope`:$($p.Name)" })
                }
            }
        } finally {
            if ($content.Zip) { $content.Zip.Dispose() }
        }
    }
    $subRoot = Get-ChildItem -LiteralPath $dir -Directory | Where-Object { $_.Name -ieq 'mods' } | Select-Object -First 1
    if ($subRoot) {
        foreach ($s in Get-ChildItem -LiteralPath $subRoot.FullName -Directory) {
            Read-Mod $s.FullName "$scope.$($s.Name.ToLowerInvariant())" $top
        }
    }
}

foreach ($mod in $active) {
    if ($mod -in 'core', 'vcmi') { continue }
    $dir = Get-ChildItem -LiteralPath $modsRoot -Directory | Where-Object { $_.Name -ieq $mod } | Select-Object -First 1
    if ($dir) { Read-Mod $dir.FullName $mod.ToLowerInvariant() $mod.ToLowerInvariant() }
}

$factions['neutral'] = 'Neutral'
foreach ($c in $modFactionCreatures) {
    if (-not $factions.Contains($c.Faction)) { $factions[$c.Faction] = Get-Pretty $c.Faction }
    $creatures.Add($c)
}

# ---------------- write ----------------
$lines = New-Object Collections.Generic.List[string]
$order = @{}; $n = 0
foreach ($k in $factions.Keys) { $order[$k] = $n++ }
foreach ($k in $factions.Keys) {
    if ($creatures | Where-Object { $_.Faction -eq $k } | Select-Object -First 1) {
        $lines.Add("F`t$k`t$($factions[$k])")
    }
}
function Get-Unique([string]$type, [string]$id) {
    $name = $id.Substring($id.IndexOf(':') + 1).ToLowerInvariant()
    if ($bare[$type][$name] -eq 1) { return 1 }
    return 0
}
$i = 0
$creatures | ForEach-Object { $_ | Add-Member -NotePropertyName Seq -NotePropertyValue ($i++) -PassThru } |
    Sort-Object @{ E = { $order[$_.Faction] } }, Level, Seq |
    ForEach-Object { $lines.Add("C`t$($_.Faction)`t$($_.Level)`t$($_.Name)`t$($_.Id)`t$(Get-Unique creature $_.Id)") }
$skills | Sort-Object Name | ForEach-Object { $lines.Add("S`t$($_.Name)`t$($_.Id)`t$(Get-Unique skill $_.Id)") }
$clsOrder = @{ Treasure = 0; Minor = 1; Major = 2; Relic = 3; Special = 4 }
$artifacts | Sort-Object @{ E = { if ($clsOrder.ContainsKey($_.Class)) { $clsOrder[$_.Class] } else { 5 } } }, Name |
    ForEach-Object { $lines.Add("A`t$($_.Class)`t$($_.Name)`t$($_.Id)`t$(Get-Unique artifact $_.Id)") }

[IO.File]::WriteAllLines($Out, $lines, [Text.Encoding]::GetEncoding(1252))

Write-Host ("Catalog {0}: {1} factions, {2} creatures, {3} skills, {4} artifacts" -f $Out,
    ($lines | Where-Object { $_ -like "F`t*" }).Count, $creatures.Count, $skills.Count, $artifacts.Count)
