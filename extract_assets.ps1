# Pulls the few Heroes III files the overlay draws with out of the game's LOD
# archives: the dialog background texture and the three bitmap fonts, plus the
# creature/skill name tables the catalog uses. Output goes to $Out.
param(
    [Parameter(Mandatory)][string]$Data,
    [string]$Out = (Join-Path $PSScriptRoot 'assets')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

$want = @('DIBOXBCK.PCX', 'BIGFONT.FNT', 'MEDFONT.FNT', 'SMALFONT.FNT',
          'CRTRAITS.TXT', 'SSTRAITS.TXT', 'ARTRAITS.TXT')

# LOD: "LOD\0", u32 type, u32 count, then 32-byte entries from offset 92:
# name[16], u32 offset, u32 size, u32 type, u32 packed size (0 = stored).
function Read-Lod([string]$path, [string[]]$names, [string]$outDir) {
    $fs = [IO.File]::OpenRead($path)
    $br = New-Object IO.BinaryReader $fs
    try {
        $fs.Position = 8
        $count = $br.ReadUInt32()
        $fs.Position = 92
        $entries = @()
        for ($i = 0; $i -lt $count; $i++) {
            $nameBytes = $br.ReadBytes(16)
            $name = [Text.Encoding]::ASCII.GetString($nameBytes).Split([char]0)[0].ToUpperInvariant()
            $entries += [pscustomobject]@{
                Name = $name; Offset = $br.ReadUInt32(); Size = $br.ReadUInt32()
                Type = $br.ReadUInt32(); Packed = $br.ReadUInt32()
            }
        }
        foreach ($e in $entries) {
            if ($names -notcontains $e.Name) { continue }
            $fs.Position = $e.Offset
            if ($e.Packed -gt 0) {
                # zlib: skip the 2-byte header, the rest is raw deflate.
                $raw = $br.ReadBytes([int]$e.Packed)
                $ms = New-Object IO.MemoryStream (, $raw)
                $ms.Position = 2
                $ds = New-Object IO.Compression.DeflateStream $ms, ([IO.Compression.CompressionMode]::Decompress)
                $buf = New-Object byte[] $e.Size
                $read = 0
                while ($read -lt $e.Size) {
                    $n = $ds.Read($buf, $read, $e.Size - $read)
                    if ($n -le 0) { break }
                    $read += $n
                }
                $ds.Dispose()
            } else {
                $buf = $br.ReadBytes([int]$e.Size)
            }
            [IO.File]::WriteAllBytes((Join-Path $outDir $e.Name), $buf)
            $script:found += $e.Name
        }
    } finally {
        $br.Dispose()
        $fs.Dispose()
    }
}

New-Item -ItemType Directory -Force $Out | Out-Null
$script:found = @()
foreach ($lod in 'H3bitmap.lod', 'H3ab_bmp.lod') {
    $p = Join-Path $Data $lod
    if (Test-Path $p) { Read-Lod $p ($want | Where-Object { $script:found -notcontains $_ }) $Out }
}

# H3 PCX -> 24-bit BMP. Header: u32 size, u32 width, u32 height; size == w*h
# means 8-bit indexed with a 768-byte RGB palette after the pixels, otherwise BGR.
function Convert-Pcx([string]$src, [string]$dst) {
    $b = [IO.File]::ReadAllBytes($src)
    $size = [BitConverter]::ToUInt32($b, 0)
    $w = [BitConverter]::ToInt32($b, 4)
    $h = [BitConverter]::ToInt32($b, 8)
    $bmp = New-Object Drawing.Bitmap $w, $h, ([Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $pal = 12 + $w * $h
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            if ($size -eq $w * $h) {
                $i = $b[12 + $y * $w + $x] * 3
                $c = [Drawing.Color]::FromArgb($b[$pal + $i], $b[$pal + $i + 1], $b[$pal + $i + 2])
            } else {
                $o = 12 + ($y * $w + $x) * 3
                $c = [Drawing.Color]::FromArgb($b[$o + 2], $b[$o + 1], $b[$o])
            }
            $bmp.SetPixel($x, $y, $c)
        }
    }
    $bmp.Save($dst, [Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
}

Add-Type -AssemblyName System.Drawing
$pcx = Join-Path $Out 'DIBOXBCK.PCX'
if (Test-Path $pcx) {
    Convert-Pcx $pcx (Join-Path $Out 'diboxbck.bmp')
    Remove-Item $pcx
}

$missing = $want | Where-Object { $script:found -notcontains $_ }
if ($missing) { Write-Warning "Not found in LODs: $($missing -join ', ')" }
Write-Host "Assets in $Out : $($script:found -join ', ')"
