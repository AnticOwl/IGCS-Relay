param(
  [ValidateSet('x86','x64','both')]
  [string]$Architecture = 'both'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'Setup-Dependencies.ps1')

function Build-Arch([string]$Arch) {
  $Preset = if ($Arch -eq 'x86') { 'vs2022-x86' } else { 'vs2022-x64' }
  $BuildPreset = if ($Arch -eq 'x86') { 'release-x86' } else { 'release-x64' }
  $Suffix = if ($Arch -eq 'x86') { 'addon32' } else { 'addon64' }

  cmake --preset $Preset
  if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $Arch" }

  cmake --build --preset $BuildPreset
  if ($LASTEXITCODE -ne 0) { throw "CMake build failed for $Arch" }

  Write-Host "Built: $Root\build\$Preset\bin\Release\IGCSDOF_UniversalBridge.$Suffix" -ForegroundColor Green
}

Push-Location $Root
try {
  if ($Architecture -eq 'x86' -or $Architecture -eq 'both') { Build-Arch 'x86' }
  if ($Architecture -eq 'x64' -or $Architecture -eq 'both') { Build-Arch 'x64' }
} finally {
  Pop-Location
}
