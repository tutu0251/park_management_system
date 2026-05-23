#Requires -Version 5.1
param(
    [Parameter(Mandatory = $true)]
    [string]$Name
)

$ErrorActionPreference = "Stop"
$FirmwareDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$IniPath = Join-Path $FirmwareDir "platformio.ini"
$Common = @("atmega8_base", "node_common", "backbone_common", "gateway_common")

$Ini = Get-Content -Path $IniPath -Raw
$Envs = [regex]::Matches($Ini, '(?m)^\[env:([^\]]+)\]') |
    ForEach-Object { $_.Groups[1].Value } |
    Where-Object {
        $_ -notin $Common -and (
            $_ -match '^node\d+$' -or $_ -match '^backbone\d*$' -or $_ -match '^gateway\d*$'
        )
    }

if ($Envs -contains $Name) {
    exit 0
}

Write-Host "[ERROR] Unknown build environment: $Name"
Write-Host ("Available: " + ($Envs -join ', '))
exit 2
