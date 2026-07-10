@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-VhidStatus.ps1" %*
exit /b %ERRORLEVEL%
