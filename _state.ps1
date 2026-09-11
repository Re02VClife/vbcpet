Write-Host '=== PORTS ==='
[System.IO.Ports.SerialPort]::GetPortNames() -join ', '
Write-Host '=== 303A/CH340 ==='
Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like '*303A*' -or $_.InstanceId -like '*1A86*' } | Select-Object Status, FriendlyName, InstanceId | Format-Table -AutoSize | Out-String
Write-Host '=== COM13 help probe ==='
$sp = New-Object System.IO.Ports.SerialPort('COM13',115200)
$sp.ReadTimeout=600; $sp.WriteTimeout=400; $sp.DtrEnable=$false; $sp.RtsEnable=$false
$enc = New-Object System.Text.UTF8Encoding($false); $sp.Encoding=$enc
$opened=$false
try { $sp.Open(); $opened=$true; Start-Sleep -Milliseconds 300; try{$null=$sp.ReadExisting()}catch{} } catch { Write-Host ('OPEN-FAIL ' + $_.Exception.Message) }
if($opened){
  foreach($ch in ('help'+[char]13+[char]10)){ try{$sp.Write([string]$ch)}catch{} }
  Start-Sleep -Milliseconds 800
  $post=''; try{$post=$sp.ReadExisting()}catch{}
  $sp.Close()
  Write-Host ('help-resp-len=' + $post.Length)
  if($post -match 'help'){ Write-Host 'APP-CONSOLE-ALIVE' } else { $d=$post -replace '[^\x20-\x7E]','.'; Write-Host ('bytes=' + $d) }
}
