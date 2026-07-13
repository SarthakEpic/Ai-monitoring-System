param(
    [Parameter(Mandatory = $true)] [string]$Episodes,
    [Parameter(Mandatory = $true)] [string]$LockedManifest,
    [Parameter(Mandatory = $true)] [string]$ReleaseManifest,
    [Parameter(Mandatory = $true)] [string]$Output
)

$ErrorActionPreference = 'Stop'
python python/certification_runner.py --episodes $Episodes --locked-manifest $LockedManifest --release-manifest $ReleaseManifest --output $Output
$exitCode = $LASTEXITCODE
if ($exitCode -eq 0) { Write-Host "Locked certification passed; inspect the generated report before enabling the certified slice." }
elseif ($exitCode -eq 2) { Write-Host "Locked certification is NOT_CERTIFIED. The report is the authoritative result."; exit 2 }
else { throw "Certification runner failed with exit code $exitCode" }
