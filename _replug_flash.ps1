$ErrorActionPreference='Continue'
# Replug-flash: wait fresh boot on COM22 -> settle 15s -> send dl -> esptool flash
$CRLF = [string][char]13 + [char]10
$py = 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw = 'D:/vscode/vbcpet/firmware'
$log = 'D:/vscode/vbcpet/_replug_flash_log.txt'
function L($m) { Add-Content $log ((Get-Date -Format 'HH:mm:ss') + ' ' + $m) }
Remove-Item $log -ErrorAction SilentlyContinue
L 'waiting for fresh boot (user replug)...'

$sw = [Diagnostics.Stopwatch]::StartNew()
$bootSeen = $false
$all = ''
$buf = New-Object byte[] 65536
$sp = $null
while ($sw.Elapsed.TotalSeconds -lt 720 -and -not $bootSeen) {
  try {
    if (-not $sp) {
      $sp = New-Object System.IO.Ports.SerialPort('COM22',115200,'None',8,'One')
      $sp.Open(); $sp.DtrEnable = $false; $sp.RtsEnable = $false
      L 'port open, listening...'
    }
    try { $n = $sp.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp.Read($buf,0,$n) | Out-Null; $chunk = [System.Text.Encoding]::UTF8.GetString($buf,0,$n); $all += $chunk; if ($chunk -match 'rst:|boot:0|ESP-IDF v5') { $bootSeen = $true; L 'BOOT-DETECTED' } } } catch {}
  } catch { if ($sp) { try { $sp.Close() } catch {}; $sp = $null }; Start-Sleep -Milliseconds 600 }
  Start-Sleep -Milliseconds 150
}
if (-not $bootSeen) { L 'NO-BOOT in 300s'; if ($sp) { $sp.Close() }; exit 1 }

# settle 15s while capturing (console ready by then)
$sw2 = [Diagnostics.Stopwatch]::StartNew()
while ($sw2.Elapsed.TotalSeconds -lt 15) {
  try { $n = $sp.BytesToRead; if ($n -gt 0) { if ($n -gt $buf.Length) { $n = $buf.Length }; $sp.Read($buf,0,$n) | Out-Null; $all += [System.Text.Encoding]::UTF8.GetString($buf,0,$n) } } catch {}
  Start-Sleep -Milliseconds 150
}
# ping console: CRLF -> expect prompt; then dl
L 'sending dl...'
try { $sp.DiscardInBuffer() } catch {}
$sp.Write($CRLF)
Start-Sleep -Milliseconds 500
try { $sp.ReadExisting() | Out-Null } catch {}
$sp.Write('dl' + $CRLF)
Start-Sleep -Milliseconds 1200
try { $sp.ReadExisting() | Out-Null } catch {}
$sp.Close()
L 'dl sent, esptool...'

$fm = & $py -m esptool --chip esp32p4 -p COM22 -b 460800 --before no_reset --after no_reset_stub --connect-attempts 8 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m '0x2000' ($fw+'/build/bootloader/bootloader.bin') '0x8000' ($fw+'/build/partition_table/partition-table.bin') '0x10000' ($fw+'/build/vbcpet.bin') 2>&1 | Out-String
$hashes = ([regex]::Matches($fm,'Hash of data verified')).Count
if ($hashes -ge 3) { L ('FLASH-OK hashes=' + $hashes) } else { L 'FLASH-FAIL'; Add-Content $log $fm; exit 1 }

