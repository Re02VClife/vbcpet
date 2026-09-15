$ErrorActionPreference='Continue'
# dl watcher: poll COM10 console, send 'dl' the moment it responds; chip enters download mode
$log='D:/vscode/vbcpet/_dl_watch_log.txt'
Remove-Item $log -ErrorAction SilentlyContinue
$sw=[Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalMinutes -lt 12){
  try {
    $sp = New-Object System.IO.Ports.SerialPort('COM10',115200,'None',8,'One')
    $sp.ReadTimeout=300
    $sp.Open(); $sp.DtrEnable=$false; $sp.RtsEnable=$false
    Start-Sleep -Milliseconds 150
    $sp.Write("`r`n")
    Start-Sleep -Milliseconds 250
    $resp = $sp.ReadExisting()
    if ($resp.Length -gt 0) {
      Add-Content $log ('ALIVE: ' + ($resp.Substring(0,[Math]::Min(200,$resp.Length))))
      $sp.Write("dl`r`n")
      Start-Sleep -Milliseconds 500
      $resp2 = $sp.ReadExisting()
      Add-Content $log ('DL-SENT resp: ' + ($resp2.Substring(0,[Math]::Min(300,$resp2.Length))))
      $sp.Close()
      Write-Host 'DL-SENT'
      break
    }
    $sp.Close()
  } catch { Start-Sleep -Milliseconds 500 }
  Start-Sleep -Milliseconds 700
}
Add-Content $log 'WATCH-END'
