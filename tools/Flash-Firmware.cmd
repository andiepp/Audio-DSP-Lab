@echo off
rem Flashes the Audio-DSP-Lab firmware; double-click it, or run "Flash-Firmware.cmd COM5".
setlocal
set PORT=%1
if "%PORT%"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Flash-Firmware.ps1"
    set /p PORT=Board's COM port, e.g. COM5:
)
if "%PORT%"=="" exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Flash-Firmware.ps1" -Port %PORT% %2 %3 %4
pause
