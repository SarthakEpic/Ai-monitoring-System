# Stage 8: Adaptive Device Baselines

Stage 8 teaches PredictiveAutoHeal what normal looks like on the current
machine.

This matters because a 4 GB RAM laptop, a gaming PC, and a server should not use
the exact same idea of normal load.

## What It Learns

The baseline engine tracks rolling normal values for:

- CPU usage
- Memory usage
- Disk free percentage
- Network throughput
- Process count
- Top-process CPU pressure
- Top-process memory pressure

It compares the live sample against the learned baseline and outputs:

- Baseline status: `WARMING_UP`, `STABLE`, `SHIFTING`, or `ANOMALOUS`
- Dominant drift metric
- Baseline anomaly score
- Confidence
- Small risk adjustment for the decision engine

## SQLite Audit

Stage 8 writes baseline samples to:

```text
adaptive_baseline_samples
```

Inspect them with:

```powershell
python adaptive_baseline_summary.py --db build\Debug\monitor.db
```

## Safety Position

Stage 8 does not execute auto-healing.

It only improves context. The baseline can nudge risk scoring, but it cannot
bypass model confidence, dry-run, planner, verification, safety policy, or
allowlist gates.
