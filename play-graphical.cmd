@echo off
cd /d "%~dp0"
if not exist "build\desktop\geyi-desktop-cn.exe" (
  echo Build first with tools/build_desktop.py.
  pause
  exit /b 1
)
start "" "build\desktop\geyi-desktop-cn.exe"
