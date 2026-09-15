$ErrorActionPreference='Continue'
$CRLF = [string][char]13 + [char]10
$py = 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw = 'D:/vscode/vbcpet/firmware'
$log = 'D:/vscode/vbcpet/_auto_flash_lcd_log.txt'
function L($m) { Add-Content $log ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) }
Remove-Item $log -ErrorAction SilentlyContinue
L '=== AUTO v2: flash+lcd pipeline started ==='

# Phase A: cycle up to 10 min: probe console -> dl -> try flash; retry until flashed
$sw = [Diagnostics.Stopwatch]::StartNew()
$flashed = $false
$cycle = 0
while ($sw.Elapsed.TotalSeconds -lt 600 -and -not $flashed) {
  $cycle++
  # A1: probe console, send dl if alive
  $dlOk = $false
  try {
    $sp = New-Object System.IO.Ports.SerialPort('COM22',115200,'None',8,'One')
    $sp.ReadTimeout = 400
    $sp.Open(); $sp.DtrEnable = $false; $sp.RtsEnable = $false
    Start-Sleep -Milliseconds 250
    try { $sp.ReadExisting() | Out-Null } catch {}
    $sp.Write($CRLF)
    Start-Sleep -Milliseconds 600
    $resp = ''
    try { $resp = $sp.ReadExisting() } catch {}
    if ($resp.Length -gt 0) {
      Start-Sleep -Milliseconds 1200
      try { $sp.ReadExisting() | Out-Null } catch {}
      $sp.Write('dl' + $CRLF)
      Start-Sleep -Milliseconds 900
      try { $sp.ReadExisting() | Out-Null } catch {}
      $dlOk = $true
      L ('cycle ' + $cycle + ': console alive -> dl sent')
    }
    $sp.Close()
  } catch { }
  if (-not $dlOk) { L ('cycle ' + $cycle + ': console silent'); Start-Sleep -Seconds 2; continue }
  # A2: try flash immediately (dl may have worked)
  L 'flashing attempt...'
  $fm = & $py -m esptool --chip esp32p4 -p COM22 -b 460800 --before no_reset --after no_reset_stub --connect-attempts 4 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m '0x2000' ($fw+'/build/bootloader/bootloader.bin') '0x8000' ($fw+'/build/partition_table/partition-table.bin') '0x10000' ($fw+'/build/vbcpet.bin') 2>&1 | Out-String
  $hashes = ([regex]::Matches($fm,'Hash of data verified')).Count
  if ($hashes -ge 3) { L ('FLASH-OK hashes=' + $hashes); $flashed = $true }
  else {
    L ('cycle ' + $cycle + ': flash attempt failed (dl race?), retrying')
  }
  Start-Sleep -Seconds 2
}
if (-not $flashed) { L 'PHASE-A-GAVEUP'; exit 1 }

# Phase C: wait for reboot marker up to 6 min (user presses RST1), then auto lcd test
$sw2 = [Diagnostics.Stopwatch]::StartNew()
$bootSeen = $false
$all = ''
$buf = New-Object byte[] 65536
$sp2 = $null
while ($sw2.Elapsed.TotalSeconds -lt 360 -and -not $bootSeen) {
  try {
    if (-not $sp2) {
      $sp2 = New-Object System.IO.Ports.SerialPort('COM22',115200,'None',8,'One')
      $sp2.Open(); $sp2.DtrEnable = $false; $sp2.RtsEnable = $false
      L 'listening for boot...'
    }
    try { $n = $sp2.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp2.Read($buf,0,$n) | Out-Null; $chunk = [System.Text.Encoding]::UTF8.GetString($buf,0,$n); $all += $chunk; if ($chunk -match 'rst:|boot:0|ESP-IDF v5') { $bootSeen = $true; L 'BOOT-DETECTED' } } } catch {}
  } catch { if ($sp2) { try { $sp2.Close() } catch {}; $sp2 = $null }; Start-Sleep -Milliseconds 800 }
  Start-Sleep -Milliseconds 150
}
if (-not $bootSeen) { L 'PHASE-C-TIMEOUT (no boot marker)'; if ($sp2) { $sp2.Close() }; exit 1 }

$sw3 = [Diagnostics.Stopwatch]::StartNew()
while ($sw3.Elapsed.TotalSeconds -lt 12) {
  try { $n = $sp2.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp2.Read($buf,0,$n) | Out-Null; $all += [System.Text.Encoding]::UTF8.GetString($buf,0,$n) } } catch {}
  Start-Sleep -Milliseconds 150
}
L 'SENDING-LCD'
try { $sp2.DiscardInBuffer() } catch {}
$sp2.Write($CRLF)
Start-Sleep -Milliseconds 400
$sp2.Write('lcd' + $CRLF)
$sw4 = [Diagnostics.Stopwatch]::StartNew()
while ($sw4.Elapsed.TotalSeconds -lt 55) {
  try { $n = $sp2.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp2.Read($buf,0,$n) | Out-Null; $all += [System.Text.Encoding]::UTF8.GetString($buf,0,$n) } } catch {}
  Start-Sleep -Milliseconds 150
}
try { $sp2.Close() } catch {}
[System.IO.File]::WriteAllText('D:/vscode/vbcpet/_lcd_log4.txt', $all, [System.Text.Encoding]::UTF8)
L 'DONE - captured lcd log'
