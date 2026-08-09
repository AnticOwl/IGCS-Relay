@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Configure-Build.ps1" -Architecture x86
if errorlevel 1 pause
endlocal
