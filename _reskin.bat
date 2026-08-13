@echo off
REM ============================================================
REM  vbcpet Live2D batch reskin - double-click to run
REM
REM  Prereq: 1) Run E:\AIHT\ComfyUI\start_comfyui.bat first, keep it open
REM         2) Character ref image at ComfyUI\input\l2d_ref.jpg
REM
REM  Edit COUNT / START below:
REM    COUNT = blocks to process (5 = test, 9999 = all 78)
REM    START = starting block index (0 = from beginning)
REM ============================================================
set "COUNT=5"
set "START=0"
set "PY=D:\vscode\vbcpet\.venv\Scripts\python.exe"
cd /d "%~dp0"

echo Batch reskinning: start=%START% count=%COUNT% (~30-60s per block)
echo Make sure ComfyUI is running at 127.0.0.1:8188
"%PY%" tools\l2d_batch.py --blocks live2d\Hiyori.blocks --count %COUNT% --start %START%

echo.
echo ============================================================
echo Done. To run ALL blocks, open _reskin.bat and set COUNT=9999.
echo Then repack textures: see docs\live2d-comfyui.md step 3-4.
echo ============================================================
pause
