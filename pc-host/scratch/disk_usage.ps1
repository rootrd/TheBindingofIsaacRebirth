$ErrorActionPreference='SilentlyContinue'
$targets = @(
  "$env:LOCALAPPDATA\Temp",
  "C:\Windows\Temp",
  "$env:LOCALAPPDATA\pip\cache",
  "$env:APPDATA\npm-cache",
  "$env:LOCALAPPDATA\npm-cache",
  "$env:USERPROFILE\.zcode\cli\exec",
  "$env:LOCALAPPDATA\Huawei",
  "$env:USERPROFILE\.hvigor",
  "$env:LOCALAPPDATA\Google\Chrome\User Data\Default\Cache",
  "C:\Windows\SoftwareDistribution\Download",
  "$env:USERPROFILE\Downloads"
)
foreach ($t in $targets) {
  if (Test-Path $t) {
    $size = (Get-ChildItem $t -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    Write-Output ("{0,10:N0} MB  {1}" -f ($size/1MB), $t)
  }
}
$d = Get-PSDrive C
Write-Output ("C: free {0:N0} MB / {1:N0} MB" -f ($d.Free/1MB), (($d.Used+$d.Free)/1MB))
