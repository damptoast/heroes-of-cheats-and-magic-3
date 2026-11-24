# Install bookkeeping: every file the installer touches is recorded in
# <vcmi>\H3Overlay\install-manifest.json, with a backup copy of what was there
# before (or "created" if nothing was). Uninstall walks the manifest backwards.
#
# A file the installer writes also gets the hash of what it wrote. If the file
# differs from that hash at the next install, something else changed it since
# (a VCMI update, the user), so the new content becomes the backup to restore.

function Get-FileHashText([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

function Open-Manifest([string]$stateDir) {
    $script:StateDir = $stateDir
    $script:ManifestPath = Join-Path $stateDir 'install-manifest.json'
    $script:BackupDir = Join-Path $stateDir 'backup'
    $script:Entries = [ordered]@{}
    if (Test-Path -LiteralPath $script:ManifestPath) {
        $m = Get-Content -LiteralPath $script:ManifestPath -Raw | ConvertFrom-Json
        foreach ($e in @($m.entries)) {
            $script:Entries[$e.path] = [pscustomobject]@{ path = $e.path; backup = $e.backup; written = $e.written; kind = $e.kind }
        }
    }
}

function Save-Manifest {
    New-Item -ItemType Directory -Force $script:StateDir | Out-Null
    $m = [pscustomobject]@{ version = 1; entries = @($script:Entries.Values) }
    [IO.File]::WriteAllText($script:ManifestPath, ($m | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))
}

# Call before changing or creating a file. Returns the path of the backup of the
# original content ($null when the file did not exist before).
function Protect-File([string]$path) {
    $key = [IO.Path]::GetFullPath($path)
    $e = $script:Entries[$key]
    $exists = Test-Path -LiteralPath $key -PathType Leaf
    if ($e) {
        # Already managed. If something else rewrote it since, that is the new original.
        if ($exists -and $e.written -and (Get-FileHashText $key) -ne $e.written) {
            New-Item -ItemType Directory -Force $script:BackupDir | Out-Null
            $bak = Join-Path $script:BackupDir ("{0:D2}-{1}" -f $script:Entries.Count, [IO.Path]::GetFileName($key))
            Copy-Item -LiteralPath $key -Destination $bak -Force
            $e.backup = $bak
        }
        return $e.backup
    }
    $bak = $null
    if ($exists) {
        New-Item -ItemType Directory -Force $script:BackupDir | Out-Null
        $bak = Join-Path $script:BackupDir ("{0:D2}-{1}" -f ($script:Entries.Count + 1), [IO.Path]::GetFileName($key))
        Copy-Item -LiteralPath $key -Destination $bak -Force
    }
    $script:Entries[$key] = [pscustomobject]@{ path = $key; backup = $bak; written = $null; kind = 'file' }
    Save-Manifest
    return $bak
}

function Write-Managed([string]$path, [string]$text) {
    $key = [IO.Path]::GetFullPath($path)
    Protect-File $key | Out-Null
    New-Item -ItemType Directory -Force (Split-Path -Parent $key) | Out-Null
    [IO.File]::WriteAllText($key, $text, (New-Object Text.UTF8Encoding $false))
    $script:Entries[$key].written = Get-FileHashText $key
    Save-Manifest
}

function Copy-Managed([string]$from, [string]$to) {
    $key = [IO.Path]::GetFullPath($to)
    Protect-File $key | Out-Null
    New-Item -ItemType Directory -Force (Split-Path -Parent $key) | Out-Null
    Copy-Item -LiteralPath $from -Destination $key -Force
    $script:Entries[$key].written = Get-FileHashText $key
    Save-Manifest
}

# Records a file that something else (a script run by the installer) creates.
function Register-Created([string]$path) {
    $key = [IO.Path]::GetFullPath($path)
    if (-not $script:Entries[$key]) {
        $script:Entries[$key] = [pscustomobject]@{ path = $key; backup = $null; written = $null; kind = 'file' }
        Save-Manifest
    }
}

# Records a folder the installer owns; uninstall deletes it with its content.
function Register-Folder([string]$path) {
    $key = [IO.Path]::GetFullPath($path)
    if (-not $script:Entries[$key]) {
        $script:Entries[$key] = [pscustomobject]@{ path = $key; backup = $null; written = $null; kind = 'folder' }
        Save-Manifest
    }
}

# Puts everything back. Files with a backup are restored, files the installer
# created are deleted, owned folders are removed. Returns a list of messages.
function Restore-All([string]$stateDir) {
    Open-Manifest $stateDir
    if (-not (Test-Path -LiteralPath $script:ManifestPath)) { throw "No install manifest in $stateDir. Nothing to uninstall." }
    $log = New-Object Collections.Generic.List[string]
    $items = @($script:Entries.Values)
    [array]::Reverse($items)
    foreach ($e in $items) {
        if ($e.kind -eq 'folder') {
            if (Test-Path -LiteralPath $e.path) { Remove-Item -LiteralPath $e.path -Recurse -Force; $log.Add("removed folder $($e.path)") }
        } elseif ($e.backup) {
            if (Test-Path -LiteralPath $e.backup) {
                Copy-Item -LiteralPath $e.backup -Destination $e.path -Force
                $log.Add("restored $($e.path)")
            } else {
                $log.Add("WARNING: backup of $($e.path) is missing, left as is")
            }
        } else {
            if (Test-Path -LiteralPath $e.path) { Remove-Item -LiteralPath $e.path -Force; $log.Add("removed $($e.path)") }
        }
    }
    return $log
}
