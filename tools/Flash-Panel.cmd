@echo off
rem Flashes the Audio-DSP-Lab panel firmware into the CYD display board;
rem double-click it, or run "Flash-Panel.cmd COM18".
setlocal

rem Windows PowerShell by its full path: "powershell" is not on every PC's PATH
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=pwsh.exe"

set "PORT=%~1"
if not "%PORT%"=="" goto flash
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Flash-Firmware.ps1"
set /p "PORT=Panel's COM port, e.g. COM18: "
if "%PORT%"=="" exit /b 1

:flash
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Flash-Firmware.ps1" -Port %PORT% -Panel %2 %3
pause
