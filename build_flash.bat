@echo off
chcp 65001 >nul
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8
set PORT=%1
if "%PORT%"=="" (
  echo Usage: build_flash.bat COM5
  exit /b 1
)
idf.py set-target esp32 || exit /b 1
idf.py build || exit /b 1
idf.py -p %PORT% flash monitor
