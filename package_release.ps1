param(
    [string]$Configuration = "Debug",
    [string]$OutputDir = "release",
    [int]$BuildParallelism = 1
)

$ErrorActionPreference = "Stop"

function Invoke-NativeChecked {
    param(
        [scriptblock]$Command,
        [string]$Name
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Invoke-NativeChecked -Name "CMake configure" -Command { cmake -S . -B build }
Invoke-NativeChecked -Name "CMake build" -Command { cmake --build build --config $Configuration -- /m:$BuildParallelism }

$target = Join-Path $OutputDir "PredictiveAutoHeal"
if (Test-Path $target) {
    Remove-Item -Recurse -Force $target
}
New-Item -ItemType Directory -Path $target | Out-Null

$runtimeFiles = @(
    "build\$Configuration\PredictiveAutoHeal.exe",
    "config.txt",
    "predict_model.py",
    "inference_service.py",
    "model_features.py",
    "labeling.py",
    "set_training_label.py",
    "training_data_summary.py",
    "process_genome_summary.py",
    "user_intent_summary.py",
    "decision_audit_summary.py",
    "heal_plan_summary.py",
    "heal_verification_summary.py",
    "safety_policy_summary.py",
    "training_label.txt",
    "PROCESS_GENOME.md",
    "USER_INTENT.md",
    "DECISION_ENGINE.md",
    "AUTO_HEAL_PLANNER.md",
    "HEALING_VERIFIER.md",
    "SAFETY_POLICY.md",
    "DATA_COLLECTION.md",
    "PRODUCTION_READINESS.md",
    "README.md",
    "TRANSFER.md",
    "requirements.txt"
)

foreach ($file in $runtimeFiles) {
    if (Test-Path $file) {
        Copy-Item $file -Destination $target -Force
    }
}

$modelFiles = @(
    "build\ai_model.joblib",
    "build\ai_model_meta.json",
    "build\model_report.json",
    "build\model_report.txt"
)

foreach ($file in $modelFiles) {
    if (Test-Path $file) {
        Copy-Item $file -Destination $target -Force
    }
}

$zipPath = Join-Path $OutputDir "PredictiveAutoHeal.zip"
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
Compress-Archive -Path (Join-Path $target "*") -DestinationPath $zipPath

Write-Host "Release folder: $target"
Write-Host "Release zip: $zipPath"
