param([string]$Port = 'COM17')
$ErrorActionPreference='Continue'
$py='D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw='D:/vscode/vbcpet/firmware'
$log='D:/vscode/vbcpet/_flash_osptek_log.txt'
Remove-Item $log -ErrorAction SilentlyContinue
Add-Content $log ('OSPTEK-FLASH ' + (Get-Date -Format o) + ' port=' + $Port)
$sw=[Diagnostics.Stopwatch]::StartNew()
$done=$false
$i=0
while($i -lt 200 -and $sw.Elapsed.TotalMinutes -lt 12){
  $i++
  Add-Content $log ('attempt '+$i+' @ '+[int]$sw.Elapsed.TotalSeconds+'s')
  $fm = (& $py -m esptool --chip esp32p4 -p $Port -b 460800 --before no_reset --after no_reset_stub --connect-attempts 3 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m '0x2000' ($fw+'/build/bootloader/bootloader.bin') '0x8000' ($fw+'/build/partition_table/partition-table.bin') '0x10000' ($fw+'/build/vbcpet.bin') 2>&1 | Out-String)
  Add-Content $log $fm
  $hashes = ([regex]::Matches($fm,'Hash of data verified')).Count
  if($fm -notmatch 'fatal error' -and $hashes -ge 3){
    Add-Content $log ('FLASH-OK hashes='+$hashes)
    Write-Host 'FLASH-OK'
    $done=$true
    break
  } else {
    Add-Content $log ('retry (hashes='+$hashes+', fatal='+($fm -match 'fatal error')+')')
  }
  Start-Sleep -Milliseconds 800
}
if(-not $done){
  Add-Content $log 'GAVE-UP-12min'
  Write-Host 'FLASH-GAVE-UP'
}
