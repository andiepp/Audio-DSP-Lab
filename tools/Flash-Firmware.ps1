<#
.SYNOPSIS
    Writes the Audio-DSP-Lab firmware into the ESP32 of the Elektor Audio-DSP-FX board.

.DESCRIPTION
    Flashes the four parts of a release (bootloader, partition table,
    boot_app0 and the application) with Espressif's esptool. The board
    keeps its stored settings (WiFi networks, stations, the analyzer's
    reference, the copy of your SigmaStudio program) unless -Erase is given.

    esptool is looked for, in this order: esptool.exe beside this script,
    esptool(.exe) on the PATH, the Arduino IDE's ESP32 core, and Python's
    "python -m esptool" (pip install esptool).

.PARAMETER Port
    The board's serial port, e.g. COM5. Without it the script lists the ports.

.PARAMETER Folder
    Where the firmware's .bin files are; by default the "firmware" folder
    beside this script (a release), or this script's own folder.

.PARAMETER Erase
    Erases the whole flash first: every stored setting is lost.

.PARAMETER Baud
    The upload speed; 460800 by default. Try 115200 if uploading fails.

.EXAMPLE
    .\Flash-Firmware.ps1 -Port COM5
#>
[CmdletBinding()]
param(
    [string] $Port,
    [string] $Folder,
    [switch] $Erase,
    [int] $Baud = 460800
)

$ErrorActionPreference = 'Stop'

function Find-Esptool {
    $beside = Join-Path $PSScriptRoot 'esptool.exe'
    if (Test-Path $beside) { return @($beside) }

    $onPath = Get-Command esptool, esptool.exe, esptool.py -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($onPath) { return @($onPath.Source) }

    $arduino = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py'
    if (Test-Path $arduino) {
        $newest = Get-ChildItem $arduino -Directory |
            Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
            ForEach-Object { Join-Path $_.FullName 'esptool.exe' } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1
        if ($newest) { return @($newest) }
    }

    foreach ($python in 'py', 'python', 'python3') {
        if (Get-Command $python -ErrorAction SilentlyContinue) {
            & $python -m esptool version *> $null
            if ($LASTEXITCODE -eq 0) { return @($python, '-m', 'esptool') }
        }
    }

    throw 'esptool was not found. Install it with "pip install esptool", or put esptool.exe (from https://github.com/espressif/esptool/releases) beside this script.'
}

if (-not $Port) {
    Write-Host 'Serial ports on this PC:'
    Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%(COM%'" |
        Sort-Object Name | ForEach-Object { Write-Host "  $($_.Name)" }
    Write-Host ''
    Write-Host 'The board shows up as "Silicon Labs CP210x USB to UART Bridge (COMn)". Run again with -Port COMn.'
    return
}

if (-not $Folder) {
    $Folder = @((Join-Path $PSScriptRoot 'firmware'), $PSScriptRoot) |
        Where-Object { Test-Path (Join-Path $_ 'AudioDspLab.bin') } | Select-Object -First 1
    if (-not $Folder) { throw "No AudioDspLab.bin beside this script; name the folder with -Folder." }
}

$parts = [ordered]@{
    '0x1000'  = 'AudioDspLab.bootloader.bin'
    '0x8000'  = 'AudioDspLab.partitions.bin'
    '0xe000'  = 'boot_app0.bin'
    '0x10000' = 'AudioDspLab.bin'
}
$arguments = @()
foreach ($offset in $parts.Keys) {
    $file = Join-Path $Folder $parts[$offset]
    if (-not (Test-Path $file)) { throw "Missing $file." }
    $arguments += $offset, $file
}

$esptool = @(Find-Esptool)   # @(): a single path must stay an array, not become a string
$command = $esptool[0]
$prefix = @($esptool | Select-Object -Skip 1)
Write-Host "esptool: $($esptool -join ' ')"
Write-Host "Close every program that uses $Port (the Audio-DSP-Lab application, a serial monitor) before flashing."

$common = $prefix + @('--chip', 'esp32', '--port', $Port, '--baud', "$Baud", '--before', 'default_reset', '--after', 'hard_reset')
if ($Erase) {
    Write-Host 'Erasing the whole flash ...'
    & $command @($common + 'erase_flash')
    if ($LASTEXITCODE -ne 0) { throw "Erasing failed (esptool exit code $LASTEXITCODE)." }
}

Write-Host "Flashing the firmware from $Folder ..."
& $command @($common + @('write_flash', '-z', '--flash_mode', 'keep', '--flash_freq', 'keep', '--flash_size', 'keep') + $arguments)
if ($LASTEXITCODE -ne 0) {
    throw "Flashing failed (esptool exit code $LASTEXITCODE). If the board does not enter its boot loader, hold BOOT on the ESP32 kit while flashing starts."
}

Write-Host ''
Write-Host 'Done. The board restarts with the new firmware; connect with the Audio-DSP-Lab application.'
