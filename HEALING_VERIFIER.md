# Stage 5: Post-Heal Verification + Safety Simulator

Stage 5 estimates whether a planned healing action is likely to help before the
app ever executes real auto-healing.

The verifier is still simulation-only. It does not terminate, suspend, pause, or
modify processes.

## Inputs

- Current `SystemSnapshot`
- Stage 3 `DecisionResult`
- Stage 4 `HealPlan`

## Outputs

Each `HealVerification` includes:

- Verification id
- Status
- Outcome label
- Estimated risk before and after
- Estimated CPU before and after
- Estimated memory before and after
- Estimated disk before and after
- Estimated network before and after
- Risk delta estimate
- Confidence
- Success criteria
- Failure criteria
- Evidence string

## Statuses

- `NOT_NEEDED`: no active healing plan exists.
- `SIMULATED_PASS`: the plan is likely to help.
- `SIMULATED_WEAK`: the plan may help, but confidence/impact is limited.
- `SIMULATED_FAIL`: the plan is unlikely to help enough.
- `SIMULATED_BLOCKED`: safety gates block verification/execution.

## Why This Exists

Auto-healing should not only ask, "Can I do this safely?"

It should also ask:

- Will this actually reduce risk?
- What resource should improve?
- What does success look like?
- What does failure look like?
- How confident are we before touching the machine?

Stage 5 creates this proof layer.

## SQLite Audit

Stage 5 writes verification simulations to:

```text
heal_verifications
```

Inspect them with:

```powershell
python heal_verification_summary.py --db build\Debug\monitor.db
```

## Current Limitation

Because real auto-heal execution is still disabled, Stage 5 currently estimates
before/after outcomes. Later, after execution exists, this layer should compare
real pre-action and post-action snapshots.
