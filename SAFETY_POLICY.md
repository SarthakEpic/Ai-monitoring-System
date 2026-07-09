# Stage 6: Safety Policy Engine + Guardrails

Stage 6 adds a strict policy layer after the decision, plan, and verification
stages.

It still does not execute auto-healing.

The policy engine decides whether a proposed action is:

- `FORBIDDEN`
- `REVIEW_REQUIRED`
- `SIMULATION_ONLY`
- `EXECUTION_ELIGIBLE`

## Inputs

- Current system snapshot
- Stage 3 `DecisionResult`
- Stage 4 `HealPlan`
- Stage 5 `HealVerification`
- Runtime config policy:
  - `AUTO_HEAL_ENABLED`
  - `AUTO_HEAL_DRY_RUN`
  - `SAFE_MODE`
  - `AUTO_HEAL_ALLOWLIST`
  - `AUTO_HEAL_DENYLIST`

## Hard Blocks

The policy engine forbids execution when:

- Target is denylisted
- Target is system/security protected
- Target is foreground or user-active
- No safe target exists
- Simulation predicts the action is unlikely to help

## Review Required

The policy engine requires user review for:

- Disk cleanup recommendations
- Network activity recommendations
- Actions that need an allowlist before future execution

## Simulation Only

The policy engine keeps plans in simulation mode when:

- `AUTO_HEAL_ENABLED=0`
- `AUTO_HEAL_DRY_RUN=1`
- `SAFE_MODE=1`
- Model/decision/plan/verification gates are not strong enough
- Policy score is below the execution threshold

## SQLite Audit

Stage 6 writes evaluations to:

```text
safety_policy_evaluations
```

Inspect them with:

```powershell
python safety_policy_summary.py --db build\Debug\monitor.db
```

## Current Safety Position

Default config keeps execution blocked:

```text
AUTO_HEAL_ENABLED=0
AUTO_HEAL_DRY_RUN=1
SAFE_MODE=1
```

That means Stage 6 can mark future actions as eligible in theory only if gates
pass and config allows it. In the current default app, real execution remains
blocked.
