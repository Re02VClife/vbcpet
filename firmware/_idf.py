#!/usr/bin/env python
"""idf.py 包装器 — 剥掉 MSYSTEM 后调用真实 idf.py (Git Bash 下 workaround)

背景: Git Bash (MSYS2) 运行库会在启动原生 Windows 子进程时强制重新注入
MSYSTEM=MINGW64, 导致真实 idf.py 的 `if 'MSYSTEM' in os.environ` 触发,
仅打印警告并跳过 main()。本包装器在纯原生 python 进程里先删掉 MSYSTEM,
再以子进程方式调用 idf.py —— 子进程继承本进程 env, 不经 MSYS 运行库。
"""
import os
import sys
import subprocess

os.environ.pop("MSYSTEM", None)

IDF_PATH = r"D:\Espressif\frameworks\esp-idf-v5.5.3"
IDF_TOOLS_PATH = r"D:\Espressif"
IDF_PYTHON_ENV_PATH = r"D:\Espressif\python_env\idf5.5_py3.11_env"
os.environ["IDF_PATH"] = IDF_PATH
os.environ["IDF_TOOLS_PATH"] = IDF_TOOLS_PATH
os.environ["IDF_PYTHON_ENV_PATH"] = IDF_PYTHON_ENV_PATH
os.environ["ESP_ROM_ELF_DIR"] = r"D:\Espressif\tools\esp-rom-elfs\20241011"

# 工具链 PATH (与 _b.bat 一致)
_tool_paths = [
    os.path.join(IDF_PYTHON_ENV_PATH, "Scripts"),
    r"D:\Espressif\tools\cmake\3.30.2\bin",
    r"D:\Espressif\tools\ninja\1.12.1",
    r"D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin",
    r"D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin",
    r"D:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
    r"D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250514\openocd-esp32\bin",
    r"D:\Espressif\tools\esp-rom-elfs\20240305",
    r"D:\Espressif\tools\ccache\4.12.1",
    os.path.join(IDF_PATH, "tools"),
]
os.environ["PATH"] = ";".join(_tool_paths) + os.pathsep + os.environ.get("PATH", "")

idf_py = os.path.join(IDF_PATH, "tools", "idf.py")
assert os.path.exists(idf_py), "IDF not found: %s" % idf_py

sys.exit(subprocess.call([sys.executable, idf_py] + sys.argv[1:]))
