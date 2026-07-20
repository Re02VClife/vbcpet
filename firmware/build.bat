@echo off
REM vbcpet firmware build script — ESP32-P4 Phase 0 bring-up
REM 用法: 双击此文件 或 在 CMD 中运行 .\build.bat

call D:\Espressif\frameworks\esp-idf-v5.5.3\export.bat
if errorlevel 1 (
    echo ERROR: ESP-IDF export failed. Check path: D:\Espressif\frameworks\esp-idf-v5.5.3\
    pause
    exit /b 1
)

cd /d %~dp0

if not exist "build" (
    echo === First build: setting target to esp32p4 ===
    idf.py set-target esp32p4
)

echo === Building ===
idf.py build

if errorlevel 1 (
    echo === BUILD FAILED ===
    pause
    exit /b 1
)

echo === Build OK ===
echo.
echo To flash: idf.py -p COM16 flash monitor
echo (change COM16 to your actual port)
pause
