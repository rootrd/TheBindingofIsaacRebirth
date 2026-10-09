$ErrorActionPreference='SilentlyContinue'
Get-ChildItem "$env:LOCALAPPDATA\Temp" -Force -ErrorAction SilentlyContinue |
  ForEach-Object {
    $s = 0
    if ($_.PSIsContainer) {
      $s = (Get-ChildItem $_.FullName -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    } else { $s = $_.Length }
    [PSCustomObject]@{ MB = [math]::Round($s/1MB,0); Name = $_.Name; Modified = $_.LastWriteTime }
  } | Sort-Object MB -Descending | Select-Object -First 12 | Format-Table -AutoSize | Out-String -Width 200
