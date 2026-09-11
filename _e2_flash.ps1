# _e2_flash.ps1 — V1 板自动烧录：等芯片静默(稳定下载等待) → esptool → 失败提示复位重试
$ErrorActionPreference = 'Continue'
$port = 'COM13'
$baud = 115200
$py   = 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$base = 'D:\vscode\vbcpet\firmware\build'

function Test-Port {
  param([int]$sec, [double]$needIdle)
  $sp = New-Object System.IO.Ports.SerialPort $port, $baud
  $sp.ReadTimeout = 150
  try { $sp.Open() } catch { return 'busy' }
  $last = 0; $saw = $false
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.Elapsed.TotalSeconds -lt $sec) {
    if ($sp.BytesToRead -gt 0) { $sp.ReadExisting() | Out-Null; $saw = $true; $last = $sw.Elapsed.TotalSeconds }
    Start-Sleep -Milliseconds 40
  }
  $sp.Close()
  $idle = $sw.Elapsed.TotalSeconds - $last
  if (-not $saw) { return 'silent' }                 # 无任何数据：已在等待(好) 或 未上电
  if ($idle -ge $needIdle) { return 'quiet' }        # 有数据但已安静：稳定在 waiting(最好)
  return 'active'                                    # 还在打印/复位循环
}

$round = 0
while ($round -lt 8) {
  $round++
  Write-Host ""
  Write-Host ("===== 第 {0} 轮 =====" -f $round)
  Write-Host ">>> 如果其它窗口还开着 COM13(监听/esptool)，先关掉它们"
  $st = Test-Port 4 1.5
  Write-Host ("端口状态: {0}" -f $st)
  if ($st -eq 'busy')    { Write-Host "!! COM13 被占用，请关闭其它窗口后重跑本脚本"; break }
  if ($st -eq 'active')  { Write-Host "芯片仍在活跃输出(复位/打印)，等它安静 3 秒再试..."; Start-Sleep 3; continue }

  Write-Host ">>> 芯片静默 = 稳定在下载等待。启动 esptool ..."
  & $py -m esptool --chip esp32p4 -p $port -b $baud --before no_reset --after hard_reset --no-stub --connect-attempts 30 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m 0x2000 "$base\bootloader\bootloader.bin" 0x8000 "$base\partition_table\partition-table.bin" 0x10000 "$base\vbcpet.bin"
  $code = $LASTEXITCODE
  Write-Host ("esptool exit = {0}" -f $code)
  if ($code -eq 0) { Write-Host "===== 烧录成功！====="; break }

  Read-Host ">>> 失败。请轻触 R11 一下再松开(芯片复位进下载模式)，等 2 秒后按回车重试"
}
Write-Host "脚本结束。如果 8 轮全败，把最后几轮输出贴给助理。"
