#!/usr/bin/env python
"""idf.py 包装器 — 剥掉 MSYSTEM 后调用真 idf.py (Git Bash 下必须, 见 firmware/_idf.py)"""
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
_tool_paths = [
    os.path.join(IDF_PYTHON_ENV_PATH, "Scripts"),
    r"D:\Espressif\tools\cmake\3.30.2\bin",
    r"D:\Espressif\tools\ninja\1.12.1",
    r"D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin",
    r"D:\Espressif\tools\esp-rom-elfs\20240305",
    r"D:\Espressif\tools\ccache\4.12.1",
    os.path.join(IDF_PATH, "tools"),
]
os.environ["PATH"] = ";".join(_tool_paths) + os.pathsep + os.environ.get("PATH", "")
idf_py = os.path.join(IDF_PATH, "tools", "idf.py")
sys.exit(subprocess.call([sys.executable, idf_py] + sys.argv[1:]))
