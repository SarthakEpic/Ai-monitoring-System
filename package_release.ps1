param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$OutputDir = "release",
    [int]$BuildParallelism = 1,
    [switch]$DevelopmentDryRun,
    [string]$CertificateThumbprint = "",
    [string]$TimestampServer = "",
    [switch]$UseMachineCertificateStore
)

$ErrorActionPreference = "Stop"

function Invoke-NativeChecked {
    param([scriptblock]$Command, [string]$Name)
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE" }
}

function Sign-ReleaseFile {
    param([string]$Path, [string]$Thumbprint, [string]$TimestampUrl, [bool]$MachineStore)
    $signTool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if (-not $signTool) { throw "signtool.exe was not found. Install the Windows SDK and make SignTool available on PATH." }
    $arguments = @('sign', '/sha1', $Thumbprint, '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256', '/v')
    if ($MachineStore) { $arguments += '/sm' }
    $arguments += $Path
    & $signTool.Source @arguments
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed for $Path with exit code $LASTEXITCODE" }
}

function Assert-ReleaseSignature {
    param([string]$Path, [string]$Thumbprint)
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid') { throw "Production release file is not validly signed: $Path ($($signature.Status))" }
    if ($Thumbprint -and $signature.SignerCertificate.Thumbprint -ne $Thumbprint) { throw "Unexpected signing certificate for $Path" }
}

if (-not $DevelopmentDryRun -and (-not $CertificateThumbprint -or -not $TimestampServer)) {
    throw "Production packaging requires -CertificateThumbprint and -TimestampServer. Use -DevelopmentDryRun only for non-deployable CI/local validation."
}

Invoke-NativeChecked -Name "CMake configure" -Command { cmake -S . -B build }
Invoke-NativeChecked -Name "CMake build" -Command { cmake --build build --config $Configuration -- /m:$BuildParallelism }

$runtimeFiles = @(
    "build\$Configuration\PredictiveAutoHeal.exe",
    "build\$Configuration\PredictiveAutoHealBrowserHost.exe",
    "build\$Configuration\Aegis99ActuatorService.exe",
    "config.txt", "install_actuator_service.ps1", "install_startup.ps1", "install_browser_integration.ps1",
    "predict_model.py", "inference_service.py", "model_features.py", "labeling.py", "set_training_label.py",
    "training_data_summary.py", "process_genome_summary.py", "user_intent_summary.py", "decision_audit_summary.py",
    "heal_plan_summary.py", "heal_verification_summary.py", "safety_policy_summary.py", "runtime_health_summary.py",
    "adaptive_baseline_summary.py", "autopilot_summary.py", "benchmark_lab.py", "training_label.txt", "requirements.txt",
    "README.md", "THREAT_MODEL.md", "PRODUCTION_READINESS.md", "FAILURE_CASES.md", "DATA_COLLECTION.md",
    "docs\PHASE4_STATUS.md", "docs\PHASE3_STATUS.md", "docs\DATA_CONTRACT_V3.md"
)

if (-not $DevelopmentDryRun) {
    foreach ($file in $runtimeFiles | Where-Object { $_ -match '\.exe$' }) {
        if (-not (Test-Path $file)) { throw "Required production binary missing: $file" }
        Sign-ReleaseFile -Path (Resolve-Path $file) -Thumbprint $CertificateThumbprint -TimestampUrl $TimestampServer -MachineStore $UseMachineCertificateStore
    }
}

$target = Join-Path $OutputDir "PredictiveAutoHeal"
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
New-Item -ItemType Directory -Path $target | Out-Null
foreach ($file in $runtimeFiles) { if (Test-Path $file) { Copy-Item $file -Destination $target -Force } }
foreach ($directory in @("integrations", "benchmarks", "schemas")) {
    if (Test-Path $directory) { Copy-Item $directory -Destination (Join-Path $target $directory) -Recurse -Force }
}

if (-not $DevelopmentDryRun) {
    Get-ChildItem -Path $target -Recurse -File -Include *.exe,*.dll | ForEach-Object { Assert-ReleaseSignature -Path $_.FullName -Thumbprint $CertificateThumbprint }
}

$hashes = @{}
Get-ChildItem -Path $target -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($target.Length).TrimStart('\')
    $hashes[$relative] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$metadata = [ordered]@{
    schema_version = 1
    status = if ($DevelopmentDryRun) { "development_unsigned_not_deployable" } else { "signed_release_candidate" }
    configuration = $Configuration
    certificate_thumbprint = if ($DevelopmentDryRun) { $null } else { $CertificateThumbprint }
    artifact_hashes = $hashes
}
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $target "release_manifest.json") -Encoding utf8

$zipPath = Join-Path $OutputDir "PredictiveAutoHeal.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path (Join-Path $target "*") -DestinationPath $zipPath
Write-Host "Release folder: $target"
Write-Host "Release zip: $zipPath"
Write-Host "Status: $($metadata.status)"
