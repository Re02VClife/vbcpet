
param([string]$Port="COM8",[int]$Baud=115200,[int]$Seconds=8,[string]$Out="")
$p = New-Object System.IO.Ports.SerialPort $Port,$Baud,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One
$p.Handshake = [System.IO.Ports.Handshake]::None
$p.DtrEnable = $false
$p.RtsEnable = $false
$p.ReadTimeout = 300
try { $p.Open() } catch { Write-Host ("OPEN FAIL: " + $_.Exception.Message); exit 1 }
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$bytes = New-Object System.Collections.Generic.List[byte]
$buf = New-Object byte[] 4096
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
  try { $n = $p.Read($buf,0,$buf.Length); if ($n -gt 0) { for($i=0;$i -lt $n;$i++){ $bytes.Add($buf[$i]) } } } catch { Start-Sleep -Milliseconds 20 }
}
$p.Close()
if ($Out -ne "") { [System.IO.File]::WriteAllBytes($Out, $bytes.ToArray()) }
$txt = [System.Text.Encoding]::UTF8.GetString($bytes.ToArray()) -replace "[^\x20-\x7E\r\n\u4E00-\u9FFF]", "."
Write-Host ("== captured " + $bytes.Count + " bytes in " + [int]$sw.Elapsed.TotalSeconds + "s ==")
Write-Host $txt
if ($bytes.Count -eq 0) { Write-Host "== NO DATA ==" }
