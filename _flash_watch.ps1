$ErrorActionPreference='Continue'
$py='D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw='D:\vscode\vbcpet\firmware'
$log='D:\vscode\vbcpet\_flash_round1.txt'
Remove-Item $log -ErrorAction SilentlyContinue
$sw=[Diagnostics.Stopwatch]::StartNew()
Add-Content $log ('FLASH-WATCH START ' + (Get-Date -Format o))
Add-Content $log '等待下载模式: 把 GPIO35(pin62)->GND, GPIO36(pin63)->3V3, 断电再上电 (最多 15 分钟)'
$connected=$false
for($i=1; $i -le 1100; $i++){
  if($sw.Elapsed.TotalMinutes -gt 15){ break }
  $m = (& $py -m esptool --chip esp32p4 -p COM13 -b 460800 --before no_reset --after no_reset_stub --no-stub --connect-attempts 1 read_mac 2>&1 | Out-String)
  if($m -match 'MAC:'){
    $connected=$true
    Add-Content $log ('CONNECTED at ' + [int]$sw.Elapsed.TotalSeconds + 's:')
    Add-Content $log $m
    break
  }
  Start-Sleep -Milliseconds 600
}
if(-not $connected){
  Add-Content $log ('NO CONNECTION in 15min, ended at ' + [int]$sw.Elapsed.TotalSeconds + 's')
  exit 1
}
Add-Content $log 'FLASHING 460800 ...'
$fm = (& $py -m esptool --chip esp32p4 -p COM13 -b 460800 --before no_reset --after no_reset_stub write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m "0x2000" ($fw+'\build\bootloader\bootloader.bin') "0x8000" ($fw+'\build\partition_table\partition-table.bin') "0x10000" ($fw+'\build\vbcpet.bin') 2>&1 | Out-String)
Add-Content $log $fm
if($fm -match 'Hash of data verified'){
  Add-Content $log 'FLASH OK'
} else {
  $fm2 = (& $py -m esptool --chip esp32p4 -p COM13 -b 115200 --before no_reset --after no_reset_stub write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m "0x2000" ($fw+'\build\bootloader\bootloader.bin') "0x8000" ($fw+'\build\partition_table\partition-table.bin') "0x10000" ($fw+'\build\vbcpet.bin') 2>&1 | Out-String)
  Add-Content $log 'RETRY at 115200:'
  Add-Content $log $fm2
  if($fm2 -match 'Hash of data verified'){ Add-Content $log 'FLASH OK (115200)' } else { Add-Content $log 'FLASH FAILED' }
}
Add-Content $log ('END ' + [int]$sw.Elapsed.TotalSeconds + 's')
Write-Host ('FLASH-WATCH DONE connected=' + $connected)
