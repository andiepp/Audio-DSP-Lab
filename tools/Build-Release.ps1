<#
.SYNOPSIS
    Builds an Audio-DSP-Lab release: firmware binaries, the Windows
    application and the documentation, zipped for a GitHub release.

.DESCRIPTION
    1. Compiles the firmware with arduino-cli (ESP32 core 3.3.10, board
       ESP32 PICO-D4, partition scheme Minimal SPIFFS, the libraries in
       firmware/libraries), and the front panel's firmware for the
       ESP32-2432S028R display board (board ESP32 Dev Module, LVGL).
    2. Runs the tests and publishes the application as one self-contained
       AudioDspLab.exe for Windows x64 (no .NET installation needed).
    3. Builds the manual's PDF (needs Python with reportlab; skipped otherwise).
    4. Writes to dist/:
         Audio-DSP-Lab-<version>-windows-x64.zip   application, firmware, flash tool, documents
         Audio-DSP-Lab-firmware-<version>.zip      the firmware alone

.PARAMETER ArduinoCli
    arduino-cli.exe; by default the one on the PATH or the one inside the Arduino IDE 2.

.PARAMETER Python
    A python.exe with reportlab, for the PDFs; by default py or python from the PATH.

.PARAMETER SkipTests
    Publishes without running the tests first.

.EXAMPLE
    .\tools\Build-Release.ps1
#>
[CmdletBinding()]
param(
    [string] $ArduinoCli,
    [string] $Python,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'

function Step($text) { Write-Host "`n=== $text" -ForegroundColor Cyan }

function Find-ArduinoCli {
    if ($ArduinoCli) { return $ArduinoCli }
    $onPath = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
        "$env:ProgramFiles\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
    )
    foreach ($candidate in $candidates) { if (Test-Path $candidate) { return $candidate } }
    throw 'arduino-cli was not found; name it with -ArduinoCli.'
}

# --- version ---------------------------------------------------------------------------
$config = Get-Content (Join-Path $root 'firmware\AudioDspLab\Config.h') -Raw
$major = [regex]::Match($config, 'FIRMWARE_VERSION_MAJOR\s+(\d+)').Groups[1].Value
$minor = [regex]::Match($config, 'FIRMWARE_VERSION_MINOR\s+(\d+)').Groups[1].Value
$version = "$major.$minor"
Write-Host "Audio-DSP-Lab $version"

Remove-Item $build, $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $build, $dist | Out-Null

# --- firmware ----------------------------------------------------------------------------
Step 'Firmware'
$cli = Find-ArduinoCli
$fqbn = 'esp32:esp32:pico32:PartitionScheme=min_spiffs'
$firmwareOut = Join-Path $build 'firmware-raw'
& $cli core list | Select-String 'esp32:esp32' | ForEach-Object { Write-Host $_ }
& $cli compile --fqbn $fqbn --libraries (Join-Path $root 'firmware\libraries') --warnings default `
    --output-dir $firmwareOut (Join-Path $root 'firmware\AudioDspLab')
if ($LASTEXITCODE -ne 0 -and -not (Test-Path (Join-Path $firmwareOut 'AudioDspLab.ino.bin'))) {
    throw "The firmware did not compile (exit code $LASTEXITCODE)."
}

$firmware = Join-Path $build 'firmware'
New-Item -ItemType Directory -Force $firmware | Out-Null
Copy-Item (Join-Path $firmwareOut 'AudioDspLab.ino.bin') (Join-Path $firmware 'AudioDspLab.bin')
Copy-Item (Join-Path $firmwareOut 'AudioDspLab.ino.bootloader.bin') (Join-Path $firmware 'AudioDspLab.bootloader.bin')
Copy-Item (Join-Path $firmwareOut 'AudioDspLab.ino.partitions.bin') (Join-Path $firmware 'AudioDspLab.partitions.bin')
Copy-Item (Join-Path $firmwareOut 'AudioDspLab.ino.merged.bin') (Join-Path $firmware 'AudioDspLab-merged.bin')
$core = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32" -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
Copy-Item (Join-Path $core.FullName 'tools\partitions\boot_app0.bin') $firmware
Copy-Item (Join-Path $PSScriptRoot 'Flash-Firmware.ps1'), (Join-Path $PSScriptRoot 'Flash-Firmware.cmd'), (Join-Path $PSScriptRoot 'Flash-Panel.cmd') $firmware

Step 'Panel firmware'
$panelOut = Join-Path $build 'panel-raw'
& $cli compile --fqbn 'esp32:esp32:esp32:PartitionScheme=huge_app' --libraries (Join-Path $root 'firmware\libraries') --warnings default `
    --output-dir $panelOut (Join-Path $root 'firmware\AudioDspLabPanel')
if ($LASTEXITCODE -ne 0 -and -not (Test-Path (Join-Path $panelOut 'AudioDspLabPanel.ino.merged.bin'))) {
    throw "The panel firmware did not compile (exit code $LASTEXITCODE)."
}
Copy-Item (Join-Path $panelOut 'AudioDspLabPanel.ino.merged.bin') (Join-Path $firmware 'AudioDspLabPanel-merged.bin')
@"
Audio-DSP-Lab firmware $version for the Elektor Audio-DSP-FX board (ESP32-PICO-D4)

Flash it with Flash-Firmware.cmd (double-click) or:
  powershell -ExecutionPolicy Bypass -File Flash-Firmware.ps1 -Port COM5

Offsets, for other flash tools:
  0x1000   AudioDspLab.bootloader.bin
  0x8000   AudioDspLab.partitions.bin
  0xe000   boot_app0.bin
  0x10000  AudioDspLab.bin
or AudioDspLab-merged.bin alone at 0x0 (erases every stored setting).

The front panel (an ESP32-2432S028R "CYD" display board on the board's
panel port, see docs/Panel.md): Flash-Panel.cmd, or
  powershell -ExecutionPolicy Bypass -File Flash-Firmware.ps1 -Port COM18 -Panel
It writes AudioDspLabPanel-merged.bin at 0x0.

The compiled firmware contains arduino-audio-tools and libhelix (GPL-3.0),
the panel firmware LVGL (MIT); see THIRD-PARTY-NOTICES.md. Source: https://github.com/andiepp/Audio-DSP-Lab
"@ | Set-Content (Join-Path $firmware 'README.txt') -Encoding utf8

# --- application -----------------------------------------------------------------------
Step 'Application'
$solution = Join-Path $root 'app\AudioDspLab.slnx'
if (-not $SkipTests) {
    dotnet test $solution -c Release
    if ($LASTEXITCODE -ne 0) { throw 'The tests failed.' }
}
$appOut = Join-Path $build 'app'
dotnet publish (Join-Path $root 'app\src\AudioDspLab\AudioDspLab.csproj') -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:DebugType=none -p:Version=$version.0 -o $appOut
if ($LASTEXITCODE -ne 0) { throw 'Publishing the application failed.' }

# --- documents -------------------------------------------------------------------------
Step 'Documents'
$docs = Join-Path $build 'docs'
New-Item -ItemType Directory -Force $docs | Out-Null
if (-not $Python) { $Python = (Get-Command py, python -ErrorAction SilentlyContinue | Select-Object -First 1).Source }
if ($Python) {
    foreach ($document in 'Manual', 'Installation') {
        & $Python (Join-Path $PSScriptRoot 'Build-ManualPdf.py') (Join-Path $root "docs\$document.md") (Join-Path $docs "$document.pdf")
    }
}
if (-not (Test-Path (Join-Path $docs 'Manual.pdf'))) { Write-Warning 'No PDF built (Python with reportlab missing); the zip carries the Markdown.' }
Copy-Item (Join-Path $root 'docs\*.md') $docs

# --- packages ----------------------------------------------------------------------------
Step 'Packages'
$name = "Audio-DSP-Lab-$version-windows-x64"
$package = Join-Path $build $name
New-Item -ItemType Directory -Force $package | Out-Null
Copy-Item (Join-Path $appOut 'AudioDspLab.exe') $package
Copy-Item $firmware (Join-Path $package 'firmware') -Recurse
Copy-Item $docs (Join-Path $package 'docs') -Recurse
Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD-PARTY-NOTICES.md') $package
Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD-PARTY-NOTICES.md') (Join-Path $package 'firmware')
@"
Audio-DSP-Lab $version

1. Flash the firmware: firmware\Flash-Firmware.cmd (see docs\Installation).
2. Start AudioDspLab.exe, choose the board's COM port, Connect.
   Without a board: choose "Simulator (demo)".

Documentation: docs\Manual, docs\Installation, docs\Protocol
Project: https://github.com/andiepp/Audio-DSP-Lab
"@ | Set-Content (Join-Path $package 'README.txt') -Encoding utf8

# ZipFile writes '/' in entry names (Compress-Archive of Windows PowerShell 5.1 writes '',
# which other systems' unzip tools do not take as folders)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($package, (Join-Path $dist "$name.zip"), 'Optimal', $false)
[IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $package 'firmware'), (Join-Path $dist "Audio-DSP-Lab-firmware-$version.zip"), 'Optimal', $false)
Get-ChildItem $dist | ForEach-Object { Write-Host ("{0,-50} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB)) }
Write-Host "`nRelease $version built in $dist" -ForegroundColor Green
