@echo off
REM vbcpet 开发板 (WT9932P4-TINY) 回归构建 (SPI ST7789V 240×320) — 独立 build_dev 目录
REM 用法: 双击此文件
set "PY=D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
cd /d "%~dp0"
%PY% _idf.py -B build_dev -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.devboard" -D SDKCONFIG="build_dev/sdkconfig" build
echo DONE=%ERRORLEVEL%
pause
