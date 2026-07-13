param([switch]$Uninstall)

$ErrorActionPreference = 'Stop'
$serviceName = 'Aegis99Actuator'
$binary = Join-Path $PSScriptRoot 'Aegis99ActuatorService.exe'

if ($Uninstall) {
    sc.exe stop $serviceName 2>$null
    sc.exe delete $serviceName 2>$null
    exit 0
}
if (-not (Test-Path $binary)) { throw "Actuator binary is missing: $binary" }
# The trusted actuator does not need LocalSystem. LocalService is a built-in
# low-privilege identity; privileged operations must remain explicitly scoped.
sc.exe create $serviceName binPath= ('"' + $binary + '"') start= demand obj= 'NT AUTHORITY\LocalService' type= own
if ($LASTEXITCODE -ne 0) { throw 'Service installation failed. Run this script from an elevated PowerShell.' }
sc.exe failure $serviceName reset= 86400 actions= restart/5000
if ($LASTEXITCODE -ne 0) { throw 'Service recovery configuration failed.' }
Write-Host 'Aegis99 actuator service installed as LocalService. It is fail-closed until a trusted proof source is deployed.'
