@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Start-TinyIMX-Desktop.ps1" -Count 3
if errorlevel 1 pause
