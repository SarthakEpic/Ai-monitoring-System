param(
    [Parameter(Mandatory = $true)] [string]$PackageRoot,
    [Parameter(Mandatory = $true)] [string]$CertificateThumbprint,
    [string]$Output = ""
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $PackageRoot).Path
$manifestPath = Join-Path $root 'release_manifest.json'
if (-not (Test-Path $manifestPath)) { throw "Release manifest not found: $manifestPath" }
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
if ($manifest.status -ne 'signed_release_candidate') { throw 'Only a signed production package can be frozen.' }
if ($manifest.certificate_thumbprint -ne $CertificateThumbprint) { throw 'Package certificate thumbprint does not match the requested freeze certificate.' }
foreach ($property in $manifest.artifact_hashes.PSObject.Properties) {
    $file = Join-Path $root $property.Name
    if (-not (Test-Path $file)) { throw "Frozen artifact missing: $($property.Name)" }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $property.Value) { throw "Frozen artifact hash mismatch: $($property.Name)" }
}
Get-ChildItem -Path $root -Recurse -File -Include *.exe,*.dll | ForEach-Object {
    $signature = Get-AuthenticodeSignature -LiteralPath $_.FullName
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) { throw "Invalid frozen binary signature: $($_.Name)" }
}
$frozen = [ordered]@{
    schema_version = 1
    frozen = $true
    frozen_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    git_commit = (git rev-parse HEAD)
    artifact_hashes = $manifest.artifact_hashes
    source_release_manifest = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    certification_policy = [ordered]@{ confidence = 0.95; correct_lower_minimum = 0.99; coverage_lower_minimum = 0.70; critical_recall_lower_minimum = 0.97; severe_harm_upper_maximum = 0.001 }
}
if (-not $Output) { $Output = Join-Path $root 'frozen_release_manifest.json' }
$frozen | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Host "Frozen release manifest: $Output"
