$ErrorActionPreference='Continue'
# Boot log capture on COM22 (console UART0 @115200), 240s window
$log='D:/vscode/vbcpet/_boot_log.txt'
Remove-Item $log -ErrorAction SilentlyContinue
$sw=[Diagnostics.Stopwatch]::StartNew()
try {
  $sp = New-Object System.IO.Ports.SerialPort('COM22',115200,'None',8,'One')
  $sp.ReadTimeout=200
  $sp.Open(); $sp.DtrEnable=$false; $sp.RtsEnable=$false
  Add-Content $log ('LISTENING ' + (Get-Date -Format o))
  while($sw.Elapsed.TotalSeconds -lt 240){
    try { $chunk = $sp.ReadExisting() } catch { $chunk='' }
    if ($chunk.Length -gt 0) { Add-Content $log $chunk }
    Start-Sleep -Milliseconds 120
  }
  $sp.Close()
} catch { Add-Content $log ('ERR: ' + $_.Exception.Message) }
Add-Content $log 'LISTEN-END'
