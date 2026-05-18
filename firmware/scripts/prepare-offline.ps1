#Requires -Version 5.1
<#
.SYNOPSIS
  Download and verify all PlatformIO dependencies for offline firmware builds.

.DESCRIPTION
  Run once while connected to the internet from the firmware/ directory (or anywhere;
  the script cd's to firmware/). Populates:
    .platformio-core/  — atmelavr platform, AVR toolchain, Arduino-MiniCore
    .pio/libdeps/      — RF24 library (per environment)

  After this succeeds, builds work without network:
    pio run -e node1
    pio run -e backbone
    pio run -e gateway

  To move to another PC:
    1. Install Python 3.10+, then run python-offline\install-offline.ps1 (no network).
    2. Copy firmware/ including .platformio-core, .pio, and python-offline\wheels.
    3. Run pio run -e <env> as usual.

.PARAMETER SkipBuild
  Only install packages; do not compile all environments.
#>
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$FirmwareDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $FirmwareDir

if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
    throw "PlatformIO CLI not found. Install with: pip install platformio"
}

$Envs = @("node1", "node2", "node3", "node4", "backbone", "gateway")
$EnvArgs = $Envs | ForEach-Object { "-e"; $_ }

Write-Host "==> PlatformIO: installing packages for $($Envs -join ', ')"
& pio pkg install @EnvArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $SkipBuild) {
    Write-Host "==> PlatformIO: building all environments (verifies offline-ready toolchain)"
    & pio run @EnvArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$core = Join-Path $FirmwareDir ".platformio-core"
$libdeps = Join-Path $FirmwareDir ".pio\libdeps"
$coreMb = if (Test-Path $core) {
    [math]::Round((Get-ChildItem $core -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
} else { 0 }
$libMb = if (Test-Path $libdeps) {
    [math]::Round((Get-ChildItem $libdeps -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
} else { 0 }

Write-Host ""
Write-Host "Offline cache ready under firmware/"
Write-Host "  .platformio-core  ${coreMb} MB"
Write-Host "  .pio/libdeps      ${libMb} MB"
Write-Host ""
Write-Host "You can now disconnect and run:  pio run -e <env>"
