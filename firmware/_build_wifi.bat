@echo off
REM vbcpet WiFi 探测固件构建 (模组内 C6 + esp-hosted, W0 阶段)
REM 用法: 双击本文件, 或 _build_wifi.bat
REM
REM 三个必须的环境变量:
REM   ESP_IDF_VERSION=5.5        → esp_wifi_remote 的 Kconfig 靠它选 idf_v5.5 分支,
REM                                不设则 CONFIG_WIFI_RMT_* 全部缺失, 编译必挂
REM   IDF_COMPONENT_* 走国内镜像 → espressif.com 的组件源在这台机器上连不上
REM   SDKCONFIG=sdkconfig.wifi   → 与主线 sdkconfig (UART 推流) 隔离, 互不影响
setlocal
cd /d "%~dp0"
set "IDF_COMPONENT_REGISTRY_URL=https://components.espressif.cn/"
set "IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.cn/"
set "ESP_IDF_VERSION=5.5"
set "PY=D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
"%PY%" _idf.py -B build_wifi -D SDKCONFIG=sdkconfig.wifi build
echo DONE=%ERRORLEVEL%  (产物: build_wifi\vbcpet.bin)
pause
