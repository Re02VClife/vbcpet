$ErrorActionPreference='Continue'
$py='D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$fw='D:/vscode/vbcpet/firmware'
$enc = New-Object System.Text.UTF8Encoding($false)
# phase 1: wait for app, send dl until we see force-download echo (max 90s)
$gotdl=$false
$sw=[Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalSeconds -lt 90 -and -not $gotdl){
  $sp = New-Object System.IO.Ports.SerialPort('COM13',115200)
  $sp.ReadTimeout=300; $sp.WriteTimeout=300; $sp.DtrEnable=$false; $sp.RtsEnable=$false
  $sp.Encoding=$enc
  try {
    $sp.Open()
    try{$null=$sp.ReadExisting()}catch{}
    foreach($ch in ('dl'+[char]13+[char]10)){ try{$sp.Write([string]$ch)}catch{} }
    $sb=New-Object System.Text.StringBuilder
    $sw2=[Diagnostics.Stopwatch]::StartNew()
    while($sw2.Elapsed.TotalMilliseconds -lt 900){ try{$t=$sp.ReadExisting(); if($t){[void]$sb.Append($t)}}catch{}; Start-Sleep -Milliseconds 40 }
    $sp.Close()
    if($sb.ToString() -match 'force-download'){ $gotdl=$true; Write-Host ('DL-TRIGGERED: '+$sb.ToString()) }
  } catch {}
  if(-not $gotdl){ Start-Sleep -Milliseconds 1500 }
}
if(-not $gotdl){ Write-Host 'NO-DL-TRIGGER (app not reachable in 90s)'; exit 2 }
Write-Host 'WAIT for chip to settle into download...'; Start-Sleep -Seconds 2
# phase 2: write_flash --no-stub (retry-safe: stays in ROM download mode)
$done=$false
for($i=0;$i -lt 10;$i++){
  $fm = (& $py -m esptool --chip esp32p4 -p COM13 -b 460800 --before no_reset --after no_reset_stub --no-stub --connect-attempts 4 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m '0x2000' ($fw+'/build/bootloader/bootloader.bin') '0x8000' ($fw+'/build/partition_table/partition-table.bin') '0x10000' ($fw+'/build/vbcpet.bin') 2>&1 | Out-String)
  $h = ([regex]::Matches($fm,'Hash of data verified')).Count
  if(($fm -notmatch 'fatal error') -and ($h -ge 3)){ Write-Host ('DL-FLASH-OK hashes='+$h); $done=$true; break }
  else { Write-Host ('retry '+$i+' hashes='+$h) ; Start-Sleep -Milliseconds 1200 }
}
if(-not $done){ Write-Host 'DL-FLASH-GAVE-UP' }
