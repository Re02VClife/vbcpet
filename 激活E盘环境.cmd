@echo off
rem ============================================================
rem  vbcpet E 盘依赖环境一键进入脚本
rem  作用: 把 E 盘 Python venv 加入 PATH, 并设置 Playwright 浏览器路径,
rem        之后可直接在当前窗口运行所有 pc-host 推流脚本。
rem  用法:  call 激活E盘环境.cmd  (或用 cmd /k 激活E盘环境.cmd)
rem ============================================================

set "VBCPET_PY=E:\deps\python-venv"
set "VBCPET_PYEXE=%VBCPET_PY%\Scripts\python.exe"

if not exist "%VBCPET_PYEXE%" (
    echo [vbcpet] 未找到 E 盘 venv: %VBCPET_PYEXE%
    echo [vbcpet] 请先运行: py -3.11 -m venv E:\deps\python-venv
    exit /b 1
)

rem Playwright 浏览器装在 E 盘
set "PLAYWRIGHT_BROWSERS_PATH=E:\deps\playwright-browsers"

rem 把 E 盘 venv 的 Scripts 提到 PATH 最前, 使 python/pip 默认指向 E 盘
set "PATH=%VBCPET_PY%\Scripts;%PATH%"

echo [vbcpet] 已激活 E 盘环境:
echo   python      = %VBCPET_PYEXE%
echo   playwright 浏览器 = %PLAYWRIGHT_BROWSERS_PATH%
echo.
echo 现在可直接运行, 例如:
echo   python pc-host\stream_simple.py --mode live2d
echo   python pc-host\desktop_pet_display.py --mode png --no-p4
echo.
