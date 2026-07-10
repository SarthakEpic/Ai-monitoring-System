param(
    [switch]$Remove
)

$ErrorActionPreference = "Stop"

$appPath = Join-Path $PSScriptRoot "PredictiveAutoHeal.exe"
$name = "PredictiveAutoHealAgent"
$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"

if ($Remove) {
    Remove-ItemProperty -Path $runKey -Name $name -ErrorAction SilentlyContinue
    Write-Host "Removed PredictiveAutoHeal start-on-boot entry."
    exit 0
}

if (!(Test-Path $appPath)) {
    throw "PredictiveAutoHeal.exe was not found next to this script. Run this from the release folder."
}

New-Item -ItemType Directory -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name $name -Value "`"$appPath`" --agent"
Write-Host "Installed PredictiveAutoHeal background agent start-on-boot entry."
