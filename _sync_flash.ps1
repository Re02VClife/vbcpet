$ErrorActionPreference='Continue'
# strap 同步烧录: 监听等待 waiting for download → 立即 esptool (内置时序对齐)
$py='D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw='D:/vscode/vbcpet/firmware'
$enc = New-Object System.Text.UTF8Encoding($false)

function Test-Port {
  $sp = New-Object System.IO.Ports.SerialPort 'COM13', 115200
  $sp.ReadTimeout = 100
  try { $sp.Open(); $sp.Close(); return $true } catch { return $false }
}

# phase 0: 等端口可用
$deadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $deadline) { if (Test-Port) { break }; Start-Sleep -Milliseconds 500 }
if (-not (Test-Port)) { Write-Host 'PORT-BUSY-TIMEOUT'; exit 2 }
Write-Host 'PORT-READY - press RST1 repeatedly to cycle download mode...'

# phase 1: 反复监听等待 seeing 'waiting for download' (最多 60s)
$seen = $false
$deadline = (Get-Date).AddSeconds(60)
$sb = New-Object System.Text.StringBuilder
while ((Get-Date) -lt $deadline -and -not $seen) {
  $sp = New-Object System.IO.Ports.SerialPort 'COM13', 115200, 'None', 8, 'One'
  $sp.ReadTimeout = 150
  $sp.Encoding = $enc
  try { $sp.Open() } catch { Start-Sleep -Milliseconds 300; continue }
  $t0 = (Get-Date)
  while (((Get-Date) - $t0).TotalSeconds -lt 6 -and -not $seen) {
    try {
      $t = $sp.ReadExisting()
      if ($t.Length -gt 0) { [void]$sb.Append($t); if ($sb.ToString() -match 'waiting for download') { $seen = $true } }
    } catch {}
    Start-Sleep -Milliseconds 40
  }
  $sp.Close()
  if (-not $seen) { [void]$sb.Clear() }
}
if (-not $seen) { Write-Host 'NEVER-SAW-WAITING - chip not entering download'; exit 3 }
Write-Host ('SEEN-WAITING at ' + (Get-Date).ToString('HH:mm:ss.fff'))

# phase 2: 立即烧 (依次尝试 115200 然后 460800, 每轮连 3 秒)
foreach ($baud in @(115200, 460800)) {
  Write-Host ('TRY baud=' + $baud)
  $out = (& $py -m esptool --chip esp32p4 -p COM13 -b $baud --before no_reset --after hard_reset --no-stub --connect-attempts 6 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m '0x2000' ($fw+'/build/bootloader/bootloader.bin') '0x8000' ($fw+'/build/partition_table/partition-table.bin') '0x10000' ($fw+'/build/vbcpet.bin') 2>&1 | Out-String)
  Write-Host $out
  if ($out -match 'Hash of data verified|hard resetting|successfully') { Write-Host 'FLASH-OK'; exit 0 }
}
Write-Host 'FLASH-FAILED-BOTH-BAUDS'
exit 4