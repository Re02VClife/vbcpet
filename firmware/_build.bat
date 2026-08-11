@echo off
REM vbcpet 最终板固件构建 (MIPI-DSI ST7102 480×800) — 通过 _idf.py 包装器 (自动处理 MSYSTEM)
REM 用法: 双击此文件
set "PY=D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
cd /d "%~dp0"
%PY% _idf.py build
echo DONE=%ERRORLEVEL%
pause
