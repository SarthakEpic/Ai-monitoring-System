# PredictiveAutoHeal

PredictiveAutoHeal is a Windows desktop monitoring tool that collects local system metrics, stores them in SQLite, and combines rule-based thresholds with a Python machine-learning model to estimate reliability risk. It shows CPU, memory, disk, network, process, AI forecast, and decision-engine status in a native Win32 dashboard.

## Features

- Native C++20 Win32 dashboard.
- CPU, memory, disk, network, process count, and top-process monitoring.
- SQLite-backed metric history in `monitor.db`.
- Batched background metric persistence.
- Configurable threshold and performance profiles.
- Python model training from collected telemetry.
- Runtime AI prediction through `predict_model.py`.
- Risk decision layer that combines thresholds, anomaly signals, and model confidence.
- Portable deployment layout for copying the built app to another Windows machine.

## Project Structure

| Path | Purpose |
| --- | --- |
| `main.cpp` | Win32 dashboard, monitoring loop, runtime prediction integration. |
| `SystemMetrics.*` | System metric collection. |
| `MetricsStorage.*` | SQLite schema and metric logging. |
| `MetricsPipeline.*` | Background batching pipeline for metric writes. |
| `DecisionEngine.*` | Risk scoring and action recommendation logic. |
| `AppConfig.*` | `config.txt` parsing helpers. |
| `train_model.py` | Trains the reliability model from `monitor.db`. |
| `predict_model.py` | Runs prediction for runtime feature JSON. |
| `model_features.py` | Feature engineering and prediction payload contract. |
| `labeling.py` | Training label generation. |
| `test_model_contract.py` | Python unit tests for the model/runtime contract. |
| `TRANSFER.md` | Portable copy guide. |
| `portable/` | Ready-to-copy runtime bundle. |

## Requirements

- Windows.
- Visual Studio 2022 Build Tools or another CMake-compatible MSVC toolchain.
- CMake 3.20 or newer.
- Python 3.12 or newer.
- Python packages from `requirements.txt`:
  - `numpy`
  - `pandas`
  - `scikit-learn`
  - `joblib`

SQLite is bundled in the repository as `sqlite3.c` and `sqlite3.h`.

## Python Setup

Install the Python dependencies:

```powershell
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

If Python is not available as `python`, update `config.txt`:

```txt
PYTHON_EXE=C:\Path\To\python.exe
```

## Build

Open a Developer PowerShell or load the Visual Studio environment first:

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && powershell'
```

Configure and build:

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Debug
```

The executable is created at:

```txt
build\Debug\PredictiveAutoHeal.exe
```

The post-build step copies runtime files such as `config.txt`, `predict_model.py`, `model_features.py`, `labeling.py`, `requirements.txt`, and model artifacts into the executable directory when they exist.

## Run

From the repository root:

```powershell
.\build\Debug\PredictiveAutoHeal.exe
```

The app creates or updates `monitor.db` next to the executable and begins collecting telemetry for the local machine.

## Train The Model

Run the application first so `monitor.db` has enough metric history. Then train a model:

```powershell
python train_model.py --db build\monitor.db --model build\ai_model.joblib --meta build\ai_model_meta.json
```

Training also writes:

```txt
model_report.json
model_report.txt
```

The reliability report includes priority metrics for `CRITICAL` and `RECOVERY`.
Use those class-level precision, recall, and F1 scores when comparing model
changes; overall accuracy can hide weak recovery detection.

If training reports that there are not enough rows, leave the app running longer and try again.

## Run Tests

```powershell
python -m unittest test_model_contract.py
```

## Configuration

Runtime settings live in `config.txt`.

```txt
CPU_THRESHOLD=80
MEM_THRESHOLD=85
DISK_THRESHOLD=8
PERFORMANCE_MODE=LOW_END
AI_ALERT_THRESHOLD=70
AI_ALERT_CLEAR_THRESHOLD=55
AI_ALERT_TRIGGER_STREAK=2
AI_ALERT_CLEAR_STREAK=2
AI_PREDICT_INTERVAL_TICKS=10
AI_MODEL_TIMEOUT_MS=8000
AI_MODEL_CACHE_TTL_TICKS=30
DECISION_WARNING_THRESHOLD=45
DECISION_CRITICAL_THRESHOLD=65
PIPELINE_BATCH_SIZE=8
PIPELINE_FLUSH_MS=2000
PYTHON_EXE=python
SAFE_MODE=1
SERVICE_NAME=Spooler
```

Performance modes:

- `LOW_END`: fewer model calls and longer cache lifetime.
- `BALANCED`: moderate prediction frequency.
- `HIGH_PERFORMANCE`: more frequent prediction checks.

## Portable Deployment

Use the `portable/` folder or follow `TRANSFER.md`. A target machine needs:

- `PredictiveAutoHeal.exe`
- `config.txt`
- `predict_model.py`
- `model_features.py`
- `labeling.py`
- `ai_model.joblib`
- `ai_model_meta.json`
- `requirements.txt`
- Python dependencies installed with `python -m pip install -r requirements.txt`

Then run:

```powershell
.\PredictiveAutoHeal.exe
```

The portable app creates `monitor.db` next to the executable on the target device.

## Notes

- The AI model uses the `ai_reliability_v2` prediction contract.
- The model expects an 8-sample runtime window.
- If model files are missing or prediction fails, the C++ app can still rely on threshold and anomaly scoring.
- `SAFE_MODE=1` keeps automated healing behavior conservative.
