param([string]$Port='COM13')
$ErrorActionPreference='Continue'
$enc = New-Object System.Text.UTF8Encoding($false)
$sb = New-Object System.Text.StringBuilder
# ---- wait for boot banner on port (up to 60s) ----
$sp = $null
for($try=0; $try -lt 12; $try++){
  try {
    $sp = New-Object System.IO.Ports.SerialPort($Port,115200)
    $sp.ReadTimeout=1200; $sp.WriteTimeout=800
    $sp.DtrEnable=$false; $sp.RtsEnable=$false
    $sp.Encoding = $enc
    $sp.Open()
    break
  } catch {
    Start-Sleep -Milliseconds 800
    $sp = $null
  }
}
if($null -eq $sp){ Write-Output 'PORT-OPEN-FAIL'; exit 1 }
Start-Sleep -Milliseconds 500
try { $null = $sp.ReadExisting() } catch {}
$bootsw = [Diagnostics.Stopwatch]::StartNew()
$boot = ''
while($bootsw.Elapsed.TotalSeconds -lt 30 -and $boot -notmatch 'Main loop|main_task|vbcpet booting|Panic|abort'){
  try { $boot += $sp.ReadExisting() } catch {}
  # 若已进控制台 (无 banner 也打到 help 响应), 发 help 探测
  if($bootsw.Elapsed.TotalSeconds -gt 5 -and -not $boot.Contains('BOOT-PROBED')){
    try { $sp.Write('help' + [string][char]13 + [string][char]10) } catch {}
    $boot += 'BOOT-PROBED'
  }
  Start-Sleep -Milliseconds 300
}
[void]$sb.AppendLine('===== BOOT ====='); [void]$sb.AppendLine($boot)
# ---- run scan ----
[void]$sb.AppendLine('===== SCAN =====')
foreach($ch in 'scan' + [string][char]13 + [string][char]10){ $sp.Write([string]$ch); Start-Sleep -Milliseconds 5 }
$sw=[Diagnostics.Stopwatch]::StartNew()
$out=''
while($sw.Elapsed.TotalSeconds -lt 20){ try { $out += $sp.ReadExisting() } catch {}; if($out -match '扫描完成|scan done'){ Start-Sleep -Milliseconds 400; break }; Start-Sleep -Milliseconds 100 }
[void]$sb.AppendLine($out)
# ---- run test all ----
[void]$sb.AppendLine('===== TEST ALL =====')
foreach($ch in 'test all' + [string][char]13 + [string][char]10){ $sp.Write([string]$ch); Start-Sleep -Milliseconds 5 }
$sw.Restart(); $out=''
while($sw.Elapsed.TotalSeconds -lt 55){ try { $out += $sp.ReadExisting() } catch {}; if($out -match '自检汇总'){ Start-Sleep -Milliseconds 800; break }; Start-Sleep -Milliseconds 100 }
[void]$sb.AppendLine($out)
$sp.Close()
[System.IO.File]::WriteAllText('D:\vscode\vbcpet\_post_flash_result.txt', $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output ('VERIFY-DONE len=' + $sb.Length)
