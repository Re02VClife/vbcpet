$ErrorActionPreference='Continue'
# Auto screen test: wait for reboot -> wait app init -> send 'lcd' -> capture DSI logs
$log='D:/vscode/vbcpet/_lcd_log2.txt'
Remove-Item $log -ErrorAction SilentlyContinue
$sw=[Diagnostics.Stopwatch]::StartNew()
try {
  $sp = New-Object System.IO.Ports.SerialPort('COM22',115200,'None',8,'One')
  $sp.Open(); $sp.DtrEnable=$false; $sp.RtsEnable=$false
  $buf = New-Object byte[] 65536
  $all = ''
  $bootSeen = $false
  # phase 1: wait for boot marker up to 90s
  while ($sw.Elapsed.TotalSeconds -lt 90 -and -not $bootSeen) {
    try { $n = $sp.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp.Read($buf,0,$n) | Out-Null; $chunk = [System.Text.Encoding]::UTF8.GetString($buf,0,$n); $all += $chunk; if ($chunk -match 'rst:|boot:|ESP-IDF') { $bootSeen = $true; Add-Content $log 'BOOT-DETECTED' } } catch {}
    Start-Sleep -Milliseconds 150
  }
  if (-not $bootSeen) { Add-Content $log 'NO-BOOT-SEEN'; $sp.Close(); exit }
  # phase 2: wait app init 12s (capture meanwhile)
  $sw2=[Diagnostics.Stopwatch]::StartNew()
  while ($sw2.Elapsed.TotalSeconds -lt 12) {
    try { $n = $sp.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp.Read($buf,0,$n) | Out-Null; $all += [System.Text.Encoding]::UTF8.GetString($buf,0,$n) } } catch {}
    Start-Sleep -Milliseconds 150
  }
  # phase 3: send lcd
  Add-Content $log 'SENDING-LCD'
  try { $sp.DiscardInBuffer() } catch {}
  $sp.Write("`r`n")
  Start-Sleep -Milliseconds 400
  $sp.Write("lcd`r`n")
  $sw3=[Diagnostics.Stopwatch]::StartNew()
  while ($sw3.Elapsed.TotalSeconds -lt 50) {
    try { $n = $sp.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp.Read($buf,0,$n) | Out-Null; $all += [System.Text.Encoding]::UTF8.GetString($buf,0,$n) } } catch {}
    Start-Sleep -Milliseconds 150
  }
  $sp.Close()
  [System.IO.File]::WriteAllText('D:\vscode\vbcpet\_lcd_log2.txt', $all, [System.Text.Encoding]::UTF8)
  Add-Content $log 'DONE'
} catch { Add-Content $log ('ERR: ' + $_.Exception.Message) }
