param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [int]$BuildParallelism = 1,
    [switch]$SkipPackaging
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [string]$Name,
        [scriptblock]$Command
    )

    Write-Host "==> $Name"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repositoryRoot
try {
    Invoke-Checked -Name "CMake configure" -Command { cmake -S . -B build }
    Invoke-Checked -Name "CMake build ($Configuration)" -Command {
        cmake --build build --config $Configuration -- /m:$BuildParallelism
    }
    Invoke-Checked -Name "Native CTest suite" -Command {
        ctest --test-dir build -C $Configuration --output-on-failure
    }
    Invoke-Checked -Name "Python model and benchmark tests" -Command {
        python -m unittest test_model_contract.py test_benchmark_lab.py test_dataset_contract.py test_training_safety.py
    }
    Invoke-Checked -Name "Whitespace/static diff check" -Command { git diff --check }

    if (-not $SkipPackaging) {
        $packageOutput = Join-Path $repositoryRoot "build\package-dry-run"
        Invoke-Checked -Name "Portable package dry run" -Command {
            & .\package_release.ps1 -Configuration $Configuration -OutputDir $packageOutput -BuildParallelism $BuildParallelism
        }
    }

    Write-Host "All required checks passed."
}
finally {
    Pop-Location
}
