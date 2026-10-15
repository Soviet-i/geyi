@echo off
cd /d "%~dp0"
if not exist "build\host\geyi-cli.exe" (
  echo Build the prototype first with tools/run_host.py.
  pause
  exit /b 1
)
"build\host\geyi-cli.exe"
pause
