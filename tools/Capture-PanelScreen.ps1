<#
.SYNOPSIS
    Takes a screenshot of the Audio-DSP-Lab front panel (the CYD display board) over its USB port.

.DESCRIPTION
    The panel firmware answers "shot" on its USB port (115200 baud) by drawing
    its whole screen again and sending the pixels it writes to the display:
    "~shot <width> <height>", then for each block "~area <x1> <y1> <x2> <y2>"
    and its RGB565 pixels (high byte first), then "~end". This script puts the
    blocks together and saves a PNG, enlarged by -Scale without smoothing, its
    resolution set to 96 dpi times the scale (so a document shows it at the
    panel's own size).

    Commands sent first bring up a screen: "page home", "page app",
    "page settings", "section filter", "edit cutoff", "close".

.PARAMETER Port
    The panel's USB serial port, e.g. COM18. Close other programs using it.

.PARAMETER Path
    The PNG to write.

.PARAMETER Command
    Panel commands to send before the shot, in order.

.PARAMETER Scale
    The enlargement, 2 by default.

.EXAMPLE
    .\tools\Capture-PanelScreen.ps1 -Port COM18 -Path panel-settings.png -Command 'page settings'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Port,
    [Parameter(Mandatory)] [string] $Path,
    [string[]] $Command = @(),
    [int] $Scale = 2
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200
$serial.DtrEnable = $false            # DTR and RTS reset the ESP32
$serial.RtsEnable = $false
$serial.ReadTimeout = 30000
$serial.Open()
$stream = $serial.BaseStream

function Read-Line {
    $bytes = New-Object System.Collections.Generic.List[byte]
    while ($true) {
        $b = $stream.ReadByte()
        if ($b -lt 0) { throw 'The port closed.' }
        if ($b -eq 10) { break }
        if ($b -ne 13) { $bytes.Add([byte]$b) }
    }
    return [Text.Encoding]::ASCII.GetString($bytes.ToArray())
}

function Read-Exact([int] $count) {
    $buffer = New-Object byte[] $count
    $read = 0
    while ($read -lt $count) {
        $n = $stream.Read($buffer, $read, $count - $read)
        if ($n -le 0) { throw 'The port closed.' }
        $read += $n
    }
    return $buffer
}

try {
    foreach ($line in $Command) {
        $serial.Write("$line`n")
        Start-Sleep -Milliseconds 900
    }
    Start-Sleep -Milliseconds 300
    $serial.DiscardInBuffer()
    $serial.Write("shot`n")

    do { $line = Read-Line } until ($line -like '~shot *')
    $size = $line.Split(' ')
    $width = [int]$size[1]
    $height = [int]$size[2]
    $pixels = New-Object byte[] ($width * $height * 2)
    while ($true) {
        $line = Read-Line
        if ($line -eq '~end') { break }
        if ($line -notlike '~area *') { continue }
        $area = $line.Split(' ')
        $x1 = [int]$area[1]; $y1 = [int]$area[2]; $x2 = [int]$area[3]; $y2 = [int]$area[4]
        $rowBytes = ($x2 - $x1 + 1) * 2
        $data = Read-Exact ($rowBytes * ($y2 - $y1 + 1))
        for ($y = $y1; $y -le $y2; $y++) {
            [Array]::Copy($data, ($y - $y1) * $rowBytes, $pixels, ($y * $width + $x1) * 2, $rowBytes)
        }
    }
}
finally {
    $serial.Close()
}

# RGB565, high byte first -> 24-bit BGR rows
$image = New-Object System.Drawing.Bitmap $width, $height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$lock = $image.LockBits((New-Object System.Drawing.Rectangle 0, 0, $width, $height),
    [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $image.PixelFormat)
$bgr = New-Object byte[] ($lock.Stride * $height)
for ($y = 0; $y -lt $height; $y++) {
    for ($x = 0; $x -lt $width; $x++) {
        $i = ($y * $width + $x) * 2
        $v = ([int]$pixels[$i] -shl 8) -bor [int]$pixels[$i + 1]   # [int]: a byte shifted stays a byte
        $o = $y * $lock.Stride + $x * 3
        $bgr[$o] = [byte]((($v -band 0x1F) * 255) / 31)
        $bgr[$o + 1] = [byte](((($v -shr 5) -band 0x3F) * 255) / 63)
        $bgr[$o + 2] = [byte](((($v -shr 11) -band 0x1F) * 255) / 31)
    }
}
[Runtime.InteropServices.Marshal]::Copy($bgr, 0, $lock.Scan0, $bgr.Length)
$image.UnlockBits($lock)

$large = New-Object System.Drawing.Bitmap ($width * $Scale), ($height * $Scale)
$graphics = [System.Drawing.Graphics]::FromImage($large)
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$graphics.DrawImage($image, 0, 0, $width * $Scale, $height * $Scale)
$graphics.Dispose()
$large.SetResolution(96 * $Scale, 96 * $Scale)
$full = [IO.Path]::GetFullPath($Path)
$large.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$large.Dispose()
$image.Dispose()
Write-Host "Saved $full ($($width * $Scale) x $($height * $Scale))"
