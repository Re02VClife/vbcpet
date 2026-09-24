@echo off
REM vbcpet — 构建 C6 slave 固件 (esp-hosted CP, 目标 esp32c6)
REM 产物: firmware_c6\build\vbcpet_c6_cp.bin
setlocal
cd /d "%~dp0"
set "IDF_COMPONENT_REGISTRY_URL=https://components.espressif.cn/"
set "IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.cn/"
set "ESP_IDF_VERSION=5.5"
set "PY=D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
if not exist sdkconfig (
  "%PY%" _idf.py set-target esp32c6
)
"%PY%" _idf.py build
echo DONE=%ERRORLEVEL%
pause
