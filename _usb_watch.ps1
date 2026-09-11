$sw=[Diagnostics.Stopwatch]::StartNew()
$seen=@{}
while($sw.Elapsed.TotalSeconds -lt 120){
  $devs = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { ($_.InstanceId -like "*303A*") -or ($_.Class -eq "USB" -and $_.Status -eq "OK" -and $_.FriendlyName -like "Unknown*") }
  foreach($d in $devs){
    $key=$d.InstanceId
    if(-not $seen.ContainsKey($key)){
      $seen[$key]=1
      Write-Host ("[" + [int]$sw.Elapsed.TotalSeconds + "s] NEW: " + $d.Status + " | " + $d.FriendlyName + " | " + $d.InstanceId)
    } else {
      if($seen[$key] -lt 2 -and $d.Status -eq "OK"){ $seen[$key]=2; Write-Host ("[" + [int]$sw.Elapsed.TotalSeconds + "s] became OK: " + $d.FriendlyName) }
    }
  }
  Start-Sleep -Milliseconds 2000
}
Write-Host "=== WATCHER END ==="