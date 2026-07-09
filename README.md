# PredictiveAutoHeal

PredictiveAutoHeal is a Windows-first system observability dashboard that monitors live machine health, stores telemetry locally, and uses an AI-assisted decision layer to estimate failure risk before downtime.

The current version focuses on safe predictive monitoring: it detects resource pressure, explains risk, and shows auto-heal readiness. Destructive recovery actions are intentionally disabled until the model is reliable enough for safe automation.

## Highlights

- Native C++20 / Win32 desktop dashboard
- CPU, memory, disk, network, process-count, and top-process monitoring
- Process Genome Engine with per-process CPU, memory, I/O, lifetime, parent PID, foreground/window status, trust signals, category, safety, waste score, and expected gain
- User Intent Engine that detects active/idle/away state, foreground app, app kind, fullscreen state, focus duration, and recent active apps
- Safe Optimization Decision Engine with root-cause diagnosis, action targets, safety gates, cooldowns, dry-run recommendations, and blocked reasons
- Auto-Heal Dry-Run Planner with pre-checks, simulated execution steps, post-checks, rollback notes, readiness score, and audit trail
- SQLite time-series telemetry storage
- SQLite process intelligence history in `process_samples`
- SQLite user intent history in `user_intent_samples`
- SQLite decision audit history in `decision_audits`
- SQLite auto-heal plan history in `heal_plans`
- Background batching pipeline for low-overhead writes
- AI reliability contract with risk, confidence, class, reason, and recommended action
- Persistent local inference service so the model loads once instead of spawning Python every prediction
- Root-cause diagnosis for disk, memory, CPU, network, process count, and top-process pressure
- Decision engine with `NORMAL`, `WARNING`, and `CRITICAL` states
- Scenario-labeled data collection for model improvement
- Structured runtime event logs for prediction and alert auditing
- Low-end performance mode for weaker hardware
- GitHub Actions CI for tests and Windows CMake build
- Python contract tests for runtime inference compatibility
- Portable Windows runtime bundle

## Architecture

```text
Windows Metrics
      |
      v
SystemMetrics Collector
      |
      v
Metrics Pipeline  ---> SQLite telemetry database
      |
      v
Process Genome Engine
      |
      v
User Intent Engine
      |
      v
Feature Engineering
      |
      v
Persistent Python inference service
      |
      v
Safe Optimization Decision Engine
      |
      v
Auto-Heal Dry-Run Planner
      |
      v
Dashboard + Alerts + Auto-heal readiness
```

## Current Status

| Area | Status |
|---|---|
| Windows monitoring | Working |
| SQLite storage | Working |
| Dashboard | Working |
| Process Genome Engine | Working phase 1 |
| User Intent Engine | Working phase 2 |
| Safe Optimization Decision Engine | Working phase 3 |
| Auto-Heal Dry-Run Planner | Working phase 4 |
| AI risk prediction | Working prototype |
| Model confidence/reasons | Working prototype |
| Persistent inference service | Working |
| Scenario-labeled data collection | Working |
| Real auto-healing | Not enabled yet |
| Linux support | Planned |
| ONNX/native inference | Planned |

## Tech Stack

- C++20
- Win32 API
- SQLite
- CMake
- Python 3.12+
- NumPy, Pandas, Scikit-learn, Joblib

## Project Structure

```text
PredictiveAutoHeal/
├── main.cpp                  # Win32 dashboard and runtime orchestration
├── SystemMetrics.*           # Windows metric collection
├── MetricsStorage.*          # SQLite schema and inserts
├── MetricsPipeline.*         # Background batch writer
├── ProcessSnapshot.h         # Deep per-process telemetry model
├── ProcessTelemetry.*        # Windows process telemetry collector
├── ProcessClassifier.*       # Process category and safety classifier
├── ProcessGenome.*           # Waste/safety ranking engine
├── UserIntent.*              # Foreground app and active user intent collector
├── DecisionEngine.*          # Risk scoring, root cause, safety gates, and dry-run recommendations
├── AutoHealPlanner.*         # Dry-run healing plan generation and safety playbooks
├── AppConfig.*               # Config file parsing
├── train_model.py            # Model training and reliability report
├── predict_model.py          # Runtime prediction CLI
├── inference_service.py      # Persistent local prediction worker
├── model_features.py         # Feature engineering and prediction payload
├── labeling.py               # Label definitions and heuristic labels
├── set_training_label.py     # Scenario label switcher
├── training_data_summary.py  # Data coverage checker
├── process_genome_summary.py # Process intelligence coverage checker
├── user_intent_summary.py    # User intent coverage checker
├── decision_audit_summary.py # Decision/audit coverage checker
├── heal_plan_summary.py      # Auto-heal plan coverage checker
├── test_model_contract.py    # Runtime model contract tests
├── PROCESS_GENOME.md         # Process intelligence design notes
├── USER_INTENT.md            # User intent design notes
├── DECISION_ENGINE.md        # Safe optimization decision design notes
├── AUTO_HEAL_PLANNER.md      # Dry-run healing planner design notes
├── DATA_COLLECTION.md        # Training data collection guide
├── TRANSFER.md               # Portable deployment guide
└── requirements.txt
```

## Requirements

- Windows 10/11
- Visual Studio 2022 Build Tools
- CMake 3.20+
- Python 3.12+

SQLite is bundled as `sqlite3.c` / `sqlite3.h`.

## Build

From the project root:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Run:

```powershell
.\build\Debug\PredictiveAutoHeal.exe
```

## Python Setup

```powershell
python -m pip install -r requirements.txt
```

If Python is not on `PATH`, set it in `config.txt`:

```text
PYTHON_EXE=C:\Path\To\python.exe
```

## Configuration

Main settings live in `config.txt`:

```text
CPU_THRESHOLD=80
MEM_THRESHOLD=85
DISK_THRESHOLD=8
PERFORMANCE_MODE=LOW_END
AI_ALERT_THRESHOLD=70
AI_PREDICT_INTERVAL_TICKS=10
AI_MODEL_TIMEOUT_MS=8000
AI_INFERENCE_MODE=SERVICE
AI_SERVICE_POLL_MS=1000
AUTO_HEAL_ENABLED=0
AUTO_HEAL_DRY_RUN=1
```

Performance modes:

| Mode | Use Case |
|---|---|
| `LOW_END` | 4 GB RAM / weak devices |
| `BALANCED` | Normal laptops/desktops |
| `HIGH_PERFORMANCE` | Strong devices with faster model checks |

## Training Data Collection

The app can store scenario labels with telemetry rows. While the dashboard is running, use another PowerShell window:

```powershell
python set_training_label.py normal
python set_training_label.py warning
python set_training_label.py critical
python set_training_label.py recovery
python set_training_label.py auto
```

Check coverage:

```powershell
python training_data_summary.py --db build\Debug\monitor.db
```

Inspect collected process intelligence:

```powershell
python process_genome_summary.py --db build\Debug\monitor.db
```

Inspect collected user intent:

```powershell
python user_intent_summary.py --db build\Debug\monitor.db
```

Inspect Stage 3 decision audits:

```powershell
python decision_audit_summary.py --db build\Debug\monitor.db
```

Inspect Stage 4 dry-run healing plans:

```powershell
python heal_plan_summary.py --db build\Debug\monitor.db
```

See `DATA_COLLECTION.md` for safe collection guidance.

## Train The Model

```powershell
python train_model.py --db build\Debug\monitor.db --model build\ai_model.joblib --meta build\ai_model_meta.json --report-json build\model_report.json --report-txt build\model_report.txt
```

The training report includes:

- Class distribution
- Manual vs heuristic label counts
- Accuracy
- Per-class precision/recall
- Confusion matrix
- Feature importance
- Production-readiness warnings

## Run Tests

```powershell
python -m unittest test_model_contract.py
```

## Package A Release

```powershell
.\package_release.ps1
```

This creates:

```text
release\PredictiveAutoHeal\
release\PredictiveAutoHeal.zip
```

## Runtime Logs

The app writes JSON-lines runtime events next to the executable:

```text
runtime_events.jsonl
```

Logged events include prediction cycles, alert triggers, model source, confidence, root cause, and recommended action.

Stage 3 also records structured decision audits in SQLite:

```text
decision_audits
```

Each row includes level, risk, root cause, recommendation, action target, safety gate, blocked reason, expected gain, action confidence, dry-run state, and user intent context.

Stage 4 records dry-run healing plans in SQLite:

```text
heal_plans
```

Each plan includes status, execution mode, action type, target, readiness score, expected gain, pre-check, simulated execution step, post-check, rollback note, and safety notes.

The persistent inference service writes:

```text
ai_prediction_service.json
```

If the service is not ready, the app can temporarily fall back to the one-shot `predict_model.py` path when `AI_SERVICE_FALLBACK_TO_PROCESS=1`.

## Safety Position

PredictiveAutoHeal currently does not terminate or restart processes. Auto-heal output is advisory only:

```text
recommended_action
safe_to_heal=false
```

Real healing should only be enabled after:

- Strong CRITICAL and RECOVERY recall
- Process/service allowlists
- Cooldowns
- Dry-run mode
- Rollback strategy
- Operator-visible audit logs

The current Stage 3 decision engine and Stage 4 planner already produce dry-run recommendations, safety gates, and healing playbooks, but execution remains blocked by default.

## Portable Deployment

See `TRANSFER.md`.

The portable bundle requires:

```text
PredictiveAutoHeal.exe
config.txt
predict_model.py
inference_service.py
model_features.py
labeling.py
ai_model.joblib
ai_model_meta.json
requirements.txt
```

## Roadmap To Production

- Convert the model to ONNX Runtime for native C++ inference
- Add deeper SQLite failure counters and prediction latency metrics
- Add migration tests for old database files
- Add Windows service mode
- Add signed release packaging
- Add safe auto-healing execution after model readiness and planner-readiness gates pass
- Add Linux collector after Windows stabilizes

## Portfolio Notes

This project demonstrates systems programming, local telemetry pipelines, ML integration, model contract testing, and safety-aware product thinking. It is best presented as a predictive monitoring platform evolving toward safe auto-healing.
