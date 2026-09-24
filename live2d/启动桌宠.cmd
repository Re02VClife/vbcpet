@echo off
rem vbcpet 桌面桌宠启动脚本 (Route B: Electron)
cd /d "%~dp0electron"
if not exist node_modules (
    echo [vbcpet-pet] 首次运行, 安装 electron 依赖...
    call npm install
)
call npm start %*
