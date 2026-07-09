# Stage 7: Runtime Observability

Stage 7 adds a reliability layer for the application itself.

The goal is simple: before PredictiveAutoHeal can be trusted to optimize a real
machine, we must know whether the monitor, model path, SQLite writes, and alert
pipeline are healthy.

## What It Tracks

- Prediction path: warmup, persistent service, one-shot process, cache, fallback
- Prediction latency
- Model attempt, success, and failure counts
- Fallback rate
- SQLite write success and failure counts
- Alert count
- Inference service running state
- Runtime health status: `STARTING`, `HEALTHY`, `DEGRADED`, or `CRITICAL`

## SQLite Audit

Stage 7 writes periodic samples to:

```text
runtime_health_samples
```

Inspect them with:

```powershell
python runtime_health_summary.py --db build\Debug\monitor.db
```

## Why This Matters

This makes the app observable from the inside.

If the model silently falls back, SQLite writes fail, or prediction latency is too
high on a low-end PC, Stage 7 exposes that as a runtime health problem instead of
hiding it behind a normal-looking dashboard.

## Safety Position

Stage 7 does not execute auto-healing.

It only measures reliability and records evidence. Auto-heal execution remains
blocked by the existing dry-run, safe-mode, planner, verification, and safety
policy gates.
