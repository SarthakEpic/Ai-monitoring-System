# Stage 4: Auto-Heal Dry-Run Planner

Stage 4 turns Stage 3 recommendations into concrete healing plans.

It still does not terminate, suspend, or modify any process. The planner is a
simulation and audit layer that answers:

- What would we do?
- Which process or resource would be targeted?
- Why is that target selected?
- What must be checked before action?
- What should improve after action?
- What rollback or safety path exists?
- Why is execution blocked today?

## Inputs

- `SystemSnapshot`
- `DecisionResult`
- `DecisionPolicy`
- Process Genome candidate information
- User Intent protection state

## Outputs

Each `HealPlan` includes:

- Plan id
- Status
- Execution mode
- Action type and action name
- Target kind, target pid, and target name
- Safety gate and blocked reason
- Readiness score
- Expected gain
- Pre-check
- Simulated execution step
- Post-check
- Rollback plan
- Safety notes

## Plan Statuses

- `OBSERVE`: no healing plan is needed.
- `DRY_RUN_READY`: a non-destructive plan exists but execution is disabled.
- `REVIEW_REQUIRED`: the action needs user review, usually disk or network.
- `BLOCKED`: a safety gate blocks the plan.
- `COOLDOWN`: the same target was recently recommended.
- `READY`: all gates passed, but execution code is still not implemented.

## Safety Position

The default config keeps healing in simulation mode:

```text
AUTO_HEAL_ENABLED=0
AUTO_HEAL_DRY_RUN=1
SAFE_MODE=1
```

That means Stage 4 can build a plan, but the app will not apply it.

## SQLite Audit

Stage 4 writes plans to:

```text
heal_plans
```

Inspect them with:

```powershell
python heal_plan_summary.py --db build\Debug\monitor.db
```

## Why This Matters

Auto-healing without planning is dangerous. A serious optimizer needs a paper
trail before it gets permission to touch the machine.

This stage creates that paper trail:

- Recommendation
- Target
- Safety gate
- Pre-check
- Post-check
- Rollback note
- Human-readable reason

Future execution should only call real Windows process APIs after this planner
has produced reliable, repeated dry-run plans.
