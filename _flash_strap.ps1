$ErrorActionPreference='Continue'
$py='D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw='D:/vscode/vbcpet/firmware'
$log='D:/vscode/vbcpet/_flash_round3.txt'
Remove-Item $log -ErrorAction SilentlyContinue
Add-Content $log ('STRAP-SINGLE-SHOT LOOP ' + (Get-Date -Format o))
$sw=[Diagnostics.Stopwatch]::StartNew()
$done=$false
$i=0
while($i -lt 150 -and $sw.Elapsed.TotalMinutes -lt 10){
  $i++
  Start-Sleep -Milliseconds 600
  Add-Content $log ('attempt '+$i+' @ '+[int]$sw.Elapsed.TotalSeconds+'s')
  $fm = (& $py -m esptool --chip esp32p4 -p COM13 -b 460800 --before no_reset --after no_reset_stub --no-stub --connect-attempts 3 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m '0x2000' ($fw+'/build/bootloader/bootloader.bin') '0x8000' ($fw+'/build/partition_table/partition-table.bin') '0x10000' ($fw+'/build/vbcpet.bin') 2>&1 | Out-String)
  $h = ([regex]::Matches($fm,'Hash of data verified')).Count
  if(($fm -notmatch 'fatal error') -and ($h -ge 3)){
    Add-Content $log ('FLASH-OK hashes='+$h)
    Write-Host 'FLASH-OK'
    $done=$true
    break
  } else {
    Add-Content $log ('retry hashes='+$h)
  }
}
if(-not $done){ Add-Content $log 'GAVE-UP-10min'; Write-Host 'GAVE-UP' }
