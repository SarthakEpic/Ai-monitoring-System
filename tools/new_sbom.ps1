param(
    [string]$Output = "build\sbom.spdx.json"
)

$ErrorActionPreference = 'Stop'
$requirements = @()
if (Test-Path 'requirements.txt') {
    $requirements = Get-Content 'requirements.txt' | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object {
        $parts = $_ -split '==', 2
        [ordered]@{ name = $parts[0]; version = if ($parts.Count -gt 1) { $parts[1] } else { 'unversioned' } }
    }
}
$packages = @(
    [ordered]@{ SPDXID = 'SPDXRef-Package-PredictiveAutoHeal'; name = 'PredictiveAutoHeal'; versionInfo = (git rev-parse --short HEAD 2>$null); supplier = 'NOASSERTION'; downloadLocation = 'NOASSERTION'; filesAnalyzed = $false },
    [ordered]@{ SPDXID = 'SPDXRef-Package-SQLite'; name = 'SQLite amalgamation'; versionInfo = '3.45.0'; supplier = 'NOASSERTION'; downloadLocation = 'https://www.sqlite.org'; filesAnalyzed = $false }
)
foreach ($requirement in $requirements) {
    $packages += [ordered]@{ SPDXID = ('SPDXRef-Package-Python-' + $requirement.name); name = $requirement.name; versionInfo = $requirement.version; supplier = 'NOASSERTION'; downloadLocation = 'NOASSERTION'; filesAnalyzed = $false }
}
$document = [ordered]@{
    spdxVersion = 'SPDX-2.3'
    dataLicense = 'CC0-1.0'
    SPDXID = 'SPDXRef-DOCUMENT'
    name = 'PredictiveAutoHeal SBOM'
    documentNamespace = ('https://aegis99.local/sbom/' + [guid]::NewGuid())
    creationInfo = [ordered]@{ created = (Get-Date).ToUniversalTime().ToString('o'); creators = @('Tool: tools/new_sbom.ps1') }
    packages = $packages
}
$parent = Split-Path -Parent $Output
if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$document | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Host "SBOM: $Output"
