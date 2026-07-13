@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0New-VhidDevCert.ps1" %*
exit /b %ERRORLEVEL%
