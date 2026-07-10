# PredictiveAutoHeal Transfer Guide

## What to copy

Copy the portable folder contents to the target Windows device:

- `PredictiveAutoHeal.exe`
- `config.txt`
- `predict_model.py`
- `inference_service.py`
- `model_features.py`
- `labeling.py`
- `set_training_label.py`
- `training_data_summary.py`
- `training_label.txt`
- `DATA_COLLECTION.md`
- `ai_model.joblib`
- `ai_model_meta.json`
- `model_report.json`
- `model_report.txt`
- `requirements.txt`
- `install_startup.ps1`
- `autopilot_summary.py`
- `LOW_END_AUTOPILOT.md`
- `BACKGROUND_AGENT.md`
- `BENCHMARK_PROOF.md`

## Target device setup

Install Python 3.12 or newer, then run:

```powershell
python -m pip install -r requirements.txt
```

If Python is not on PATH, edit `config.txt` and set:

```txt
PYTHON_EXE=C:\Path\To\python.exe
```

## Run

Run:

```powershell
.\PredictiveAutoHeal.exe
```

The app creates `monitor.db` next to the EXE and starts collecting new metrics for that device.

## Background agent mode

Start hidden in the tray:

```powershell
.\PredictiveAutoHeal.exe --agent
```

Enable start-on-boot from the portable folder:

```powershell
.\install_startup.ps1
```

Remove it later with:

```powershell
.\install_startup.ps1 -Remove
```

The startup entry is per-user and does not require administrator rights. Stage 9 remains recommendation-only on the target device.
