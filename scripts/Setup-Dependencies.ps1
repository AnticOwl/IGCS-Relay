$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Root     = Split-Path -Parent $PSScriptRoot
$External = Join-Path $Root 'external'
$Target   = Join-Path $External 'reshade'
$TempRoot = Join-Path $env:TEMP 'IGCSDOF_UniversalBridge_Setup'

$ReShadeZip = Join-Path $TempRoot 'reshade-main.zip'
$ImGuiZip   = Join-Path $TempRoot 'imgui-v1.92.5-docking.zip'
$ReShadeUrl = 'https://github.com/crosire/reshade/archive/refs/heads/main.zip'
$ImGuiUrl   = 'https://github.com/ocornut/imgui/archive/refs/tags/v1.92.5-docking.zip'

function Download-File([string]$Url, [string]$Destination) {
    Write-Host "Downloading $Url"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing
    }
    catch {
        throw "Download failed: $Url`n$($_.Exception.Message)"
    }
}

New-Item -ItemType Directory -Force -Path $External | Out-Null
if (Test-Path $TempRoot) { Remove-Item $TempRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
if (Test-Path $Target) { Remove-Item $Target -Recurse -Force }

Write-Host '[1/5] Downloading ReShade SDK archive...'
Download-File $ReShadeUrl $ReShadeZip

Write-Host '[2/5] Extracting ReShade SDK...'
Expand-Archive -Path $ReShadeZip -DestinationPath $TempRoot -Force
$ReShadeExtracted = Get-ChildItem $TempRoot -Directory | Where-Object { $_.Name -like 'reshade-*' } | Select-Object -First 1
if (-not $ReShadeExtracted) { throw 'ReShade archive extraction failed.' }
Move-Item $ReShadeExtracted.FullName $Target

Write-Host '[3/5] Downloading matching Dear ImGui headers...'
Download-File $ImGuiUrl $ImGuiZip

Write-Host '[4/5] Extracting Dear ImGui...'
$ImGuiTemp = Join-Path $TempRoot 'imgui_extract'
New-Item -ItemType Directory -Force -Path $ImGuiTemp | Out-Null
Expand-Archive -Path $ImGuiZip -DestinationPath $ImGuiTemp -Force
$ImGuiExtracted = Get-ChildItem $ImGuiTemp -Directory | Select-Object -First 1
if (-not $ImGuiExtracted) { throw 'Dear ImGui archive extraction failed.' }
$ImGuiTarget = Join-Path $Target 'deps\imgui'
if (Test-Path $ImGuiTarget) { Remove-Item $ImGuiTarget -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Split-Path $ImGuiTarget -Parent) | Out-Null
Move-Item $ImGuiExtracted.FullName $ImGuiTarget

Write-Host '[5/6] Pinning ReShade add-on API compatibility to version 18...'
$ReShadeHeader = Join-Path $Target 'include\reshade.hpp'
$HeaderText = Get-Content -Path $ReShadeHeader -Raw
if ($HeaderText -notmatch '#define RESHADE_API_VERSION\s+\d+') {
    throw 'Unable to locate RESHADE_API_VERSION in reshade.hpp.'
}
$HeaderText = [regex]::Replace($HeaderText, '#define RESHADE_API_VERSION\s+\d+', '#define RESHADE_API_VERSION 18', 1)
Set-Content -Path $ReShadeHeader -Value $HeaderText -Encoding UTF8

Write-Host '[6/6] Validating SDK and ImGui headers...'
$ImGuiHeader = Join-Path $Target 'deps\imgui\imgui.h'
$ImGuiVersionLine = Select-String -Path $ImGuiHeader -Pattern '^#define IMGUI_VERSION_NUM\s+(\d+)' | Select-Object -First 1
if (-not $ImGuiVersionLine) { throw 'Unable to read IMGUI_VERSION_NUM from imgui.h.' }
$ImGuiVersionNum = [int]$ImGuiVersionLine.Matches[0].Groups[1].Value
$OverlayHeader = Join-Path $Target 'include\reshade_overlay.hpp'
$ExpectedLine = Select-String -Path $OverlayHeader -Pattern 'IMGUI_VERSION_NUM != (\d+)' | Select-Object -First 1
if (-not $ExpectedLine) { throw 'Unable to read expected ImGui version from reshade_overlay.hpp.' }
$ExpectedVersionNum = [int]$ExpectedLine.Matches[0].Groups[1].Value
if ($ImGuiVersionNum -ne $ExpectedVersionNum) { throw "ImGui version mismatch after setup: downloaded $ImGuiVersionNum, ReShade expects $ExpectedVersionNum." }
$DockingMarker = Select-String -Path $ImGuiHeader -Pattern '^#define IMGUI_HAS_DOCK' | Select-Object -First 1
if (-not $DockingMarker) { throw 'Wrong Dear ImGui package: ReShade requires the docking branch (IMGUI_HAS_DOCK is missing).' }
$DockingType = Select-String -Path $ImGuiHeader -Pattern 'typedef int ImGuiDockNodeFlags|enum ImGuiDockNodeFlags_' | Select-Object -First 1
if (-not $DockingType) { throw 'Wrong Dear ImGui package: ImGuiDockNodeFlags is missing.' }
$ApiLine = Select-String -Path $ReShadeHeader -Pattern '^#define RESHADE_API_VERSION\s+(\d+)' | Select-Object -First 1
if (-not $ApiLine) { throw 'Unable to verify RESHADE_API_VERSION after patching.' }
$ApiVersion = [int]$ApiLine.Matches[0].Groups[1].Value
if ($ApiVersion -ne 18) { throw "Unexpected ReShade API target after setup: $ApiVersion (expected 18)." }

$Required = @(
    'include\reshade.hpp',
    'include\reshade_api.hpp',
    'include\reshade_events.hpp',
    'include\reshade_overlay.hpp',
    'deps\imgui\imgui.h',
    'deps\imgui\imconfig.h'
)
foreach ($File in $Required) {
    if (-not (Test-Path (Join-Path $Target $File))) {
        throw "Missing dependency after setup: $File"
    }
}

Remove-Item $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
Write-Host 'Dependencies ready. Git is not required.' -ForegroundColor Green
