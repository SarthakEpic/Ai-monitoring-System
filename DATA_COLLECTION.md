# Training Data Collection Guide

Use scenario labels when you intentionally collect examples for the AI model.

## Labels

- `normal`: idle or light usage, system healthy.
- `warning`: moderate pressure, for example memory near threshold or disk getting low.
- `critical`: serious pressure, for example very low disk, very high memory, or heavy CPU load.
- `recovery`: after a stressful workload stops and the system returns toward normal.
- `auto`: default label when you are not collecting a specific scenario.

## How to Collect

Start the app, then change labels from another PowerShell window:

```powershell
python set_training_label.py normal
```

Run the target situation for a few minutes. Then switch to another label:

```powershell
python set_training_label.py warning
```

When you stop the stress workload, collect recovery:

```powershell
python set_training_label.py recovery
```

Recovery examples are most useful when they start under visible pressure and
then improve over several samples. Do not switch to `recovery` after the system
is already fully idle; capture the transition down from CPU, memory, disk, or
process pressure.

Return to default when done:

```powershell
python set_training_label.py auto
```

## Check Coverage

```powershell
python training_data_summary.py --db build\Debug\monitor.db
```

Aim for at least:

- `NORMAL`: 300+ rows
- `WARNING`: 300+ rows
- `CRITICAL`: 150+ rows
- `RECOVERY`: 150+ rows

Do not use unsafe workloads just to create critical data. Low disk and memory pressure examples are useful, but keep the device stable.
