@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-VhidDev.ps1" %*
exit /b %ERRORLEVEL%
