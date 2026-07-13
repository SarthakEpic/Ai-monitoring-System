param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [int]$BuildParallelism = 1
)

$ErrorActionPreference = 'Stop'
& "$PSScriptRoot\run_all_checks.ps1" -Configuration $Configuration -BuildParallelism $BuildParallelism
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$config = Get-Content (Join-Path $PSScriptRoot '..\config.txt') -Raw
foreach ($required in @('RUNTIME_MODE=MONITOR_ONLY', 'ACTION_EXECUTION_ENABLED=0', 'ACTION_GLOBAL_DISABLE=1', 'ONLINE_POLICY_ENABLED=0')) {
    if ($config -notmatch [regex]::Escape($required)) { throw "Release-readiness safety check failed: $required" }
}
& "$PSScriptRoot\new_sbom.ps1" -Output (Join-Path $PSScriptRoot '..\build\sbom.spdx.json')
python (Join-Path $PSScriptRoot '..\python\stress_lab.py') --output (Join-Path $PSScriptRoot '..\build\hard-case-corpus.json')
if ($LASTEXITCODE -ne 0) { throw 'Hard-case corpus generation failed.' }
Write-Host 'Release-readiness controls passed. No locked external certification dataset is present; release status remains NOT_CERTIFIED.'
