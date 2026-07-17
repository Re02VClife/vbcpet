Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$env:IDF_PATH = 'D:\Espressif\frameworks\esp-idf-v5.5.3'
$env:IDF_PYTHON_ENV_PATH = 'D:\Espressif\python_env\idf5.5_py3.11_env'
$env:PATH = "D:\Espressif\tools\ninja\1.12.1;D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;D:\Espressif\python_env\idf5.5_py3.11_env\Scripts;$env:PATH"
Set-Location 'c:\Users\XU\Desktop\vscode\vbcpet\firmware'
& 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' 'D:\Espressif\frameworks\esp-idf-v5.5.3\tools\idf.py' -p COM16 -b 2000000 flash
