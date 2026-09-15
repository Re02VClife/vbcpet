$ErrorActionPreference='Continue'
$CRLF = [string][char]13 + [char]10
$py = 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw = 'D:/vscode/vbcpet/firmware'
$log = 'D:/vscode/vbcpet/_sync_flash2_log.txt'
function L($m) { Add-Content $log ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) }
Remove-Item $log -ErrorAction SilentlyContinue

# open COM22 (retry until available)
$sp = $null
for ($i=0; $i -lt 20 -and -not $sp; $i++) {
  try { $sp = New-Object System.IO.Ports.SerialPort('COM22',115200,'None',8,'One'); $sp.Open(); $sp.DtrEnable=$false; $sp.RtsEnable=$false } catch { $sp=$null; Start-Sleep -Milliseconds 700 }
}
if (-not $sp) { L 'cannot open COM22'; exit 1 }
L 'listening 300s for boot...'

$sw=[Diagnostics.Stopwatch]::StartNew(); $bootSeen=$false; $buf = New-Object byte[] 65536
while ($sw.Elapsed.TotalSeconds -lt 300 -and -not $bootSeen) {
  try { $n = $sp.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp.Read($buf,0,$n) | Out-Null; $chunk = [System.Text.Encoding]::UTF8.GetString($buf,0,$n); if ($chunk -match 'rst:|boot:0|ESP-IDF v5') { $bootSeen=$true; L ('BOOT-DETECTED at ' + [int]$sw.Elapsed.TotalSeconds + 's') } } } catch {}
  Start-Sleep -Milliseconds 100
}
if (-not $bootSeen) { L 'NO-BOOT'; $sp.Close(); exit 1 }

# settle 15s
$sw2=[Diagnostics.Stopwatch]::StartNew()
while ($sw2.Elapsed.TotalSeconds -lt 15) {
  try { $n = $sp.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp.Read($buf,0,$n) | Out-Null } } catch {}
  Start-Sleep -Milliseconds 120
}

# dl with fresh-port retries (CH340 may re-enumerate on reset glitch)
$flashed = $false
for ($try=1; $try -le 3 -and -not $flashed; $try++) {
  L ('dl attempt ' + $try)
  try {
    try { if ($sp.IsOpen) { $sp.Close() } } catch {}
    Start-Sleep -Milliseconds 1500
    $sp = New-Object System.IO.Ports.SerialPort('COM22',115200,'None',8,'One')
    $sp.Open(); $sp.DtrEnable=$false; $sp.RtsEnable=$false
    Start-Sleep -Milliseconds 300
    try { $sp.ReadExisting() | Out-Null } catch {}
    $sp.Write($CRLF); Start-Sleep -Milliseconds 600
    $resp = ''; try { $resp = $sp.ReadExisting() } catch {}
    L ('prompt resp len: ' + $resp.Length)
    $sp.Write('dl' + $CRLF); Start-Sleep -Milliseconds 900
    try { $sp.ReadExisting() | Out-Null } catch {}
    $sp.Write('dl' + $CRLF); Start-Sleep -Milliseconds 900
    $sp.Close()
  } catch { L ('write err: ' + $_.Exception.Message); try { $sp.Close() } catch {}; continue }
  L 'esptool...'
  $fm = & $py -m esptool --chip esp32p4 -p COM22 -b 460800 --before no_reset --after no_reset_stub --connect-attempts 15 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m '0x2000' ($fw+'/build/bootloader/bootloader.bin') '0x8000' ($fw+'/build/partition_table/partition-table.bin') '0x10000' ($fw+'/build/vbcpet.bin') 2>&1 | Out-String
  $hashes = ([regex]::Matches($fm,'Hash of data verified')).Count
  if ($hashes -ge 3) { L ('FLASH-OK hashes=' + $hashes); $flashed = $true }
  else { L 'attempt failed, retry' }
}
if ($flashed) { 'FLASH-OK' } else { 'FLASH-FAIL-3x (see log)' }
