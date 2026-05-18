#Requires -Version 5.1
<#
.SYNOPSIS
  Build every node / backbone / gateway environment in platformio.ini (one at a time).
#>
$ErrorActionPreference = "Stop"
$FirmwareDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $FirmwareDir

$Common = @("atmega8_base", "node_common", "backbone_common", "gateway_common")
$Ini = Get-Content -Path "platformio.ini" -Raw
$Envs = [regex]::Matches($Ini, '(?m)^\[env:([^\]]+)\]') |
    ForEach-Object { $_.Groups[1].Value } |
    Where-Object {
        $_ -notin $Common -and (
            $_ -match '^node\d+$' -or $_ -match '^backbone\d*$' -or $_ -match '^gateway\d*$'
        )
    } |
    Sort-Object {
        if ($_ -match '^node(\d+)$') { return "0{0:D4}" -f [int]$Matches[1] }
        if ($_ -eq 'backbone') { return '10000' }
        if ($_ -match '^backbone(\d+)$') { return "100{0:D4}" -f [int]$Matches[1] }
        if ($_ -eq 'gateway') { return '20000' }
        if ($_ -match '^gateway(\d+)$') { return "200{0:D4}" -f [int]$Matches[1] }
        return $_
    }

if (-not $Envs.Count) {
    Write-Error "No build environments found in platformio.ini"
}

Write-Host "Building $($Envs.Count) environment(s), one at a time (avoids Windows file locks):"
Write-Host ($Envs -join ', ')
Write-Host ""

foreach ($Env in $Envs) {
    Write-Host "=== $Env ===" -ForegroundColor Cyan
    & pio run -e $Env -j 1
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "[OK] All environments built." -ForegroundColor Green
