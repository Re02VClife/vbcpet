@echo off
set "IDF_PATH=D:\Espressif\frameworks\esp-idf-v5.5.3"
set "IDF_TOOLS_PATH=D:\Espressif"
set "MSYSTEM="
set "PATH=D:\Espressif\python_env\idf5.5_py3.11_env\Scripts;D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\ninja\1.12.1;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;D:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin;D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250514\openocd-esp32\bin;D:\Espressif\tools\esp-rom-elfs\20240305;D:\Espressif\tools\ccache\4.12.1;D:\Espressif\frameworks\esp-idf-v5.5.3\tools;%PATH%"
cd /d C:\Users\XU\Desktop\vscode\vbcpet\firmware
python D:\Espressif\frameworks\esp-idf-v5.5.3\tools\idf.py build
echo DONE=%ERRORLEVEL%
