# Stage 3: Safe Optimization Decision Engine

Stage 3 converts raw monitoring into explainable optimization recommendations.

It does not execute auto-healing yet. The purpose is to decide what would be safe,
what is blocked, and why.

## Inputs

- Live CPU, memory, disk, network, and process count
- Process Genome candidates
- User Intent state and foreground protection
- AI probability, confidence, class, and reason
- Configured thresholds, allowlist, denylist, safe mode, dry-run mode, and cooldown

## Outputs

- `NORMAL`, `WARNING`, or `CRITICAL`
- Risk score
- Anomaly score
- Pressure score
- Root cause such as `cpu`, `memory`, `disk`, `network`, `process_count`, or `process`
- Recommended dry-run action
- Action target process/resource
- Safety gate
- Blocked reason
- Expected gain
- Action confidence
- Cooldown state

## Safety Gates

The engine can block a recommendation for these reasons:

- `OBSERVE_ONLY`: risk is too low
- `USER_REVIEW_REQUIRED`: disk/network action needs user approval
- `NO_SAFE_TARGET`: no safe background process candidate exists
- `DENYLIST_BLOCKED`: target is denied by policy
- `ALLOWLIST_REQUIRED`: target is not approved for execution
- `DRY_RUN_ONLY`: auto-heal is disabled, safe mode is enabled, or dry-run is enabled
- `WARNING_OBSERVE`: warning-level risk is not enough to execute healing
- `MODEL_CONFIDENCE_BLOCKED`: model confidence is below the healing gate
- `PROCESS_SAFETY_BLOCKED`: process safety score is below the healing gate
- `COOLDOWN`: this target was already recommended recently
- `READY`: all gates passed

Current project defaults keep execution disabled:

```text
AUTO_HEAL_ENABLED=0
AUTO_HEAL_DRY_RUN=1
SAFE_MODE=1
```

That means the dashboard can recommend actions, but it will not kill, pause, or
change any process.

## SQLite Audit

Stage 3 writes decision records to:

```text
decision_audits
```

Inspect them with:

```powershell
python decision_audit_summary.py --db build\Debug\monitor.db
```

This makes recommendation behavior reviewable before any future auto-heal
executor is added.

## Why This Matters

This is the bridge between monitoring and healing. A real optimizer cannot just
see high RAM and kill something. It must understand:

- Is the process important?
- Is it part of what the user is doing right now?
- Is it allowlisted or denylisted?
- Is the model confident enough?
- Has the same target been recommended recently?
- What is the expected gain?

Only after these questions are answered repeatedly and accurately should real
auto-healing execution be introduced.
