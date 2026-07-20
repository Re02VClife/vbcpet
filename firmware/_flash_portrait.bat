@echo off
echo === Building vbcpet firmware (portrait mode) ===
call D:\Espressif\frameworks\esp-idf-v5.5.3\export.bat
if errorlevel 1 (echo ERROR: ESP-IDF export failed & pause & exit /b 1)
cd /d C:\Users\XU\Desktop\vscode\vbcpet\firmware
idf.py build
if errorlevel 1 (echo === BUILD FAILED === & pause & exit /b 1)
echo.
echo === Build OK! Now flashing... ===
idf.py -p COM16 flash
echo.
echo === Done! ===
pause
