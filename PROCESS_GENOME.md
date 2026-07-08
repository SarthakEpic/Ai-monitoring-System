# Process Genome Engine

The Process Genome Engine is the foundation for the future resource autopilot.

It does not kill or modify processes. Its job is to understand processes deeply enough that later optimization decisions can be safe, explainable, and measurable.

## What It Collects

For each running process, the engine captures:

- PID and parent PID
- Process name and executable path
- CPU usage
- Working set memory
- Private memory
- Disk read/write rate
- Process lifetime
- Thread count
- Handle count
- Priority class
- Session ID
- Foreground window status
- Visible window status
- Basic trusted-path status
- Progressive Authenticode signature status

## Genome Categories

Each process is classified into one category:

- `SYSTEM_CRITICAL`
- `SECURITY`
- `FOREGROUND_APP`
- `USER_APP`
- `BROWSER_CHILD`
- `UPDATER_SYNC`
- `WINDOWS_SERVICE`
- `BACKGROUND_HELPER`
- `UNKNOWN`

## Safety Levels

Each process also gets a safety level:

- `PROTECTED`: never touch automatically.
- `USER_ACTIVE`: protect because the user is probably using it.
- `OBSERVE_ONLY`: visible or service-like, so only observe for now.
- `CANDIDATE`: possible future optimization target.
- `CAUTIOUS`: unknown or not trusted enough for automatic optimization.

## Scores

The engine calculates:

- `waste_score`: how much resource pressure the process appears to create.
- `importance_score`: how important/risky the process appears to be.
- `safety_score`: how safe the process appears to be for future optimization.
- `expected_gain_mb`: rough memory recovery estimate for future dry-run recommendations.

## Storage

The top ranked process samples are stored in SQLite in:

```text
process_samples
```

Samples are indexed and retained for 7 days to avoid uncontrolled database growth on low-end machines.

This creates the data foundation for future learning:

- Which processes are repeatedly wasteful?
- Which categories dominate low-end device pressure?
- Which future actions actually improve CPU/RAM/disk?
- Which processes should always be protected on a device?

## How To Inspect It

After running the app for 30-60 seconds:

```powershell
python process_genome_summary.py --db build\Debug\monitor.db
```

The summary shows:

- total process samples
- category and safety distribution
- top recent waste candidates
- expected memory gain
- recommendation and reason

## Current Safety Position

Phase 1 is observation-only.

No process is suspended, killed, priority-lowered, or modified.

The next phase should build the User Intent Engine, then dry-run optimization recommendations.
