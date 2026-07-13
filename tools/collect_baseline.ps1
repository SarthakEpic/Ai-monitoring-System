param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [int]$ProcessId = 0,
    [int]$SampleSeconds = 30,
    [string]$OutputPath = "",
    [switch]$Launch
)

$ErrorActionPreference = "Stop"

function Get-GitValue {
    param([string[]]$Arguments)
    try {
        $value = & git @Arguments 2>$null
        if ($LASTEXITCODE -eq 0) { return ($value | Select-Object -First 1).Trim() }
    }
    catch {
    }
    return "unavailable"
}

function Get-ProcessSample {
    param([int]$TargetProcessId, [int]$Seconds, [int]$LogicalProcessors)

    $first = Get-Process -Id $TargetProcessId -ErrorAction Stop
    $firstCpu = $first.CPU
    $firstTime = [DateTimeOffset]::UtcNow
    $workingSets = @([double]$first.WorkingSet64)
    Start-Sleep -Seconds $Seconds
    $second = Get-Process -Id $TargetProcessId -ErrorAction Stop
    $elapsed = ([DateTimeOffset]::UtcNow - $firstTime).TotalSeconds
    $cpuPercent = if ($elapsed -gt 0 -and $LogicalProcessors -gt 0) {
        (($second.CPU - $firstCpu) / $elapsed / $LogicalProcessors) * 100.0
    }
    else { $null }
    $workingSets += [double]$second.WorkingSet64

    return [ordered]@{
        sample_seconds = $elapsed
        cpu_percent = $cpuPercent
        working_set_mb_min = [Math]::Round((($workingSets | Measure-Object -Minimum).Minimum / 1MB), 2)
        working_set_mb_max = [Math]::Round((($workingSets | Measure-Object -Maximum).Maximum / 1MB), 2)
        wakeup_frequency_hz = "not_available_from_win32_process_api"
        loop_duration_ms = "not_available_until_runtime_scheduler_instrumentation"
        collector_durations_ms = "not_available_until_collector_cost_registry"
        inference_latency_ms = "read_runtime_health_database_when_available"
        ipc_or_file_latency_ms = "not_available_until_typed_ipc"
        database_write_latency_ms = "not_available_until_storage_timing_instrumentation"
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$binary = Join-Path $repositoryRoot "build\$Configuration\PredictiveAutoHeal.exe"
if (-not (Test-Path $binary)) {
    throw "Missing application binary: $binary. Build it first with cmake --build build --config $Configuration."
}

if ($Launch) {
    $started = Start-Process -FilePath $binary -PassThru
    $ProcessId = $started.Id
    Start-Sleep -Seconds 3
}

$operatingSystem = Get-CimInstance Win32_OperatingSystem
$computerSystem = Get-CimInstance Win32_ComputerSystem
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$featureFlags = @{}
Get-Content -LiteralPath (Join-Path $repositoryRoot "config.txt") | ForEach-Object {
    if ($_ -match '^\s*([A-Z0-9_]+)=(.*)$') {
        $featureFlags[$matches[1]] = $matches[2].Trim()
    }
}

$modelMetadataPath = Join-Path $repositoryRoot "build\ai_model_meta.json"
$modelId = "not_present"
if (Test-Path $modelMetadataPath) {
    try {
        $metadata = Get-Content -Raw -LiteralPath $modelMetadataPath | ConvertFrom-Json
        $modelId = if ($metadata.model_id) { $metadata.model_id } elseif ($metadata.generated_at) { "generated:$($metadata.generated_at)" } else { "present_without_model_id" }
    }
    catch {
        $modelId = "metadata_unreadable"
    }
}

$measurement = if ($ProcessId -gt 0) {
    Get-ProcessSample -TargetProcessId $ProcessId -Seconds ([Math]::Max(1, $SampleSeconds)) -LogicalProcessors $processor.NumberOfLogicalProcessors
}
else {
    [ordered]@{ status = "not_sampled"; reason = "Pass -ProcessId for an existing monitor process or -Launch to start a monitor-only sample." }
}

$result = [ordered]@{
    schema_version = 1
    captured_at_utc = [DateTimeOffset]::UtcNow.ToString("o")
    build = [ordered]@{
        git_commit = Get-GitValue @("rev-parse", "HEAD")
        git_branch = Get-GitValue @("branch", "--show-current")
        cmake = ((& cmake --version | Select-Object -First 1).Trim())
        compiler = "MSVC via configured CMake generator"
        architecture = $processor.AddressWidth
        configuration = $Configuration
        binary_size_bytes = (Get-Item $binary).Length
        model_id = $modelId
        schema_versions = [ordered]@{
            telemetry = "unversioned_current_schema"
            benchmark = 1
            model_contract = "ai_reliability_v2"
        }
        feature_flags = $featureFlags
    }
    environment = [ordered]@{
        windows_name = $operatingSystem.Caption
        windows_version = $operatingSystem.Version
        windows_build = $operatingSystem.BuildNumber
        cpu = $processor.Name.Trim()
        logical_processors = $processor.NumberOfLogicalProcessors
        total_memory_mb = [Math]::Round($computerSystem.TotalPhysicalMemory / 1MB, 0)
    }
    measurements = $measurement
    evidence_status = "local_development_baseline_not_certification"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $timestamp = [DateTimeOffset]::UtcNow.ToString("yyyyMMdd-HHmmss")
    $OutputPath = Join-Path $repositoryRoot "benchmarks\baseline-$timestamp.json"
}

$parent = Split-Path -Parent $OutputPath
if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Baseline written to $OutputPath"
if ($Launch) {
    Write-Host "The launched process remains running (PID $ProcessId). Stop it manually when you finish observing it."
}
