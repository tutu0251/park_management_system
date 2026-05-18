#Requires -Version 5.1
<#
.SYNOPSIS
  Download all pip wheels for offline PlatformIO install.

.DESCRIPTION
  Fills ../wheels/ with platformio==6.1.19 and every dependency needed for:
    pip install --no-index --find-links wheels -r requirements.txt
  Targets the Python version running this script (3.10+ recommended) on Windows amd64.
#>
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Wheels = Join-Path $Root "wheels"
$Req = Join-Path $Root "requirements.txt"

if (-not (Test-Path $Wheels)) { New-Item -ItemType Directory -Path $Wheels | Out-Null }

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "Python not found on PATH."
}

$ver = python -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
Write-Host "==> Downloading wheels for Python $ver (current machine ABI)"
Write-Host "    Destination: $Wheels"

& python -m pip install --upgrade pip wheel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Bootstrap wheels (upgrade pip on offline machines if needed).
& python -m pip download pip setuptools wheel -d $Wheels
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Recursive download: platformio + all dependencies (wheels and sdists if no wheel exists).
& python -m pip download -r $Req -d $Wheels
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$files = Get-ChildItem $Wheels -File
$mb = [math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host ""
Write-Host "Done: $($files.Count) files, ${mb} MB in wheels/"
Write-Host "Offline install:  cd firmware\python-offline  ;  .\install-offline.ps1"
