$ErrorActionPreference='SilentlyContinue'
$before = (Get-PSDrive C).Free

# 1) User temp: everything older than 24h (skip files in use)
Get-ChildItem "$env:LOCALAPPDATA\Temp" -Force -ErrorAction SilentlyContinue |
  Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-1) } |
  Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

# 2) Old zcode session exec logs (keep the current session)
$cur = 'sess_f755ce04-10bc-4679-a1cb-e2e3e8aa0489'
Get-ChildItem "$env:USERPROFILE\.zcode\cli\exec" -Directory -Force -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -ne $cur } |
  Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

# 3) npm cache verify-free cleanup (safe, rebuildable)
Remove-Item "$env:LOCALAPPDATA\npm-cache\*" -Recurse -Force -ErrorAction SilentlyContinue

$after = (Get-PSDrive C).Free
Write-Output ("Freed {0:N0} MB; C: free now {1:N0} MB" -f (($after-$before)/1MB), ($after/1MB))
