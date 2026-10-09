$ErrorActionPreference='Stop'
# 1) ensure WSL is fully stopped
wsl --shutdown
Start-Sleep -Seconds 3

# 2) move the directory to the proper location
if (Test-Path 'E:\WSLUbuntu2404') {
  if (-not (Test-Path 'E:\WSL')) { New-Item -ItemType Directory -Path 'E:\WSL' | Out-Null }
  if (Test-Path 'E:\WSL\Ubuntu2404') { throw 'target already exists' }
  Move-Item 'E:\WSLUbuntu2404' 'E:\WSL\Ubuntu2404'
  Write-Output 'moved to E:\WSL\Ubuntu2404'
}

# 3) fix the registry BasePath
Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Lxss' | ForEach-Object {
  $p = Get-ItemProperty $_.PSPath
  if ($p.DistributionName -eq 'Ubuntu-24.04') {
    Set-ItemProperty $_.PSPath -Name BasePath -Value 'E:\WSL\Ubuntu2404'
    Write-Output ('registry updated: ' + (Get-ItemProperty $_.PSPath).BasePath)
  }
}
