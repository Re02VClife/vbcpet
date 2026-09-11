$ErrorActionPreference='Continue'
$ocd='D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\bin\openocd.exe'
$cfg='D:\vscode\vbcpet\firmware\esp32p4-usj-flash.cfg'
$tcl='D:\vscode\vbcpet\firmware\usj_flash.tcl'
$build='D:\vscode\vbcpet\firmware\build'
$log='D:\vscode\vbcpet\_usj_flash_round1.txt'
Remove-Item $log -ErrorAction SilentlyContinue
Add-Content $log ('USJ-FLASH-WATCH ' + (Get-Date -Format o))
$sw=[Diagnostics.Stopwatch]::StartNew()
$found=$false
while($sw.Elapsed.TotalMinutes -lt 20){
  $dev = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like '*VID_303A*PID_0012*' }
  if($dev){ $found=$true; Add-Content $log ('DEVICE FOUND at ' + [int]$sw.Elapsed.TotalSeconds + 's: ' + $dev.FriendlyName); break }
  Start-Sleep -Seconds 2
}
if(-not $found){ Add-Content $log 'NO DEVICE in 20min'; Write-Host 'NO-DEVICE'; exit 2 }
Start-Sleep -Seconds 2
Add-Content $log 'RUNNING OPENOCD (builtin USB-JTAG)...'
Push-Location $build
$out = (& $ocd -f $cfg -f $tcl 2>&1 | Out-String)
Pop-Location
Add-Content $log $out
if(($out -match 'programmed') -and ($out -match 'Target 0')){
  Add-Content $log 'USJ FLASH: images programmed (check for errors above)'
  Write-Host 'FLASH-DONE'
} else {
  Add-Content $log ('USJ FLASH PROBLEM exit='+$LASTEXITCODE)
  Write-Host 'FLASH-PROBLEM'
}
