@echo off
REM ASCII-only: build vbcpet firmware with osptek st7102 driver
set IDF_TOOLS_PATH=D:\Espressif
set IDF_PYTHON_ENV_PATH=D:\Espressif\python_env\idf5.5_py3.11_env
call D:\Espressif\frameworks\esp-idf-v5.5.3\export.bat
cd /d D:\vscode\vbcpet\firmware
idf.py -B build build
echo BUILD_EXIT=%ERRORLEVEL%
