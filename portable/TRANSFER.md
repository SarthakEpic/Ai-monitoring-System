# PredictiveAutoHeal Transfer Guide

## What to copy

Copy the portable folder contents to the target Windows device:

- `PredictiveAutoHeal.exe`
- `config.txt`
- `predict_model.py`
- `model_features.py`
- `ai_model.joblib`
- `ai_model_meta.json`
- `requirements.txt`

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
