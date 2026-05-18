#Requires -Version 5.1
<#
.SYNOPSIS
  Install PlatformIO from local wheels (no internet).

.DESCRIPTION
  Run from firmware/python-offline/ after copying this folder from an online machine.
  Requires Python 3.10+ on PATH.

.PARAMETER User
  Install with pip --user (default). Use -User:$false for a venv or global install.
#>
param(
    [bool]$User = $true
)

$ErrorActionPreference = "Stop"
$Dir = $PSScriptRoot
$Wheels = Join-Path $Dir "wheels"
$Req = Join-Path $Dir "requirements.txt"

if (-not (Test-Path $Wheels) -or -not (Get-ChildItem $Wheels -Filter "*.whl" -ErrorAction SilentlyContinue)) {
    throw "No wheels in $Wheels. Run scripts/download-wheels.ps1 while online first."
}

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "Python not found on PATH."
}

Write-Host "==> pip / setuptools / wheel (offline bootstrap)"
& python -m pip install --no-index --find-links $Wheels pip setuptools wheel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$pipArgs = @(
    "install",
    "--no-index",
    "--find-links", $Wheels,
    "-r", $Req
)
if ($User) { $pipArgs += "--user" }

Write-Host "==> platformio (offline) from $Wheels"
& python -m pip @pipArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$pio = Get-Command pio -ErrorAction SilentlyContinue
if (-not $pio) {
    $scripts = Join-Path $env:APPDATA "Python\Python310\Scripts"
    if (Test-Path $scripts) {
        Write-Host ""
        Write-Host "Add PlatformIO to PATH for this session:"
        Write-Host "  `$env:Path += `";$scripts`""
    }
}

Write-Host ""
Write-Host "Installed. Verify with:  pio --version"
Write-Host "Then from firmware/:     .\scripts\prepare-offline.ps1  (if .platformio-core not copied yet)"
