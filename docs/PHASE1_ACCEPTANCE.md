# Aegis-99 Phase 1 Acceptance Evidence

Status: LOCAL IMPLEMENTATION COMPLETE (2026-07-13)

Automatic actions remain disabled. This document records repository-verified
implementation gates only; it is not a production certificate.

| Acceptance gate | Evidence | Status |
|---|---|---|
| Reproducible Windows checks | `tools/run_all_checks.ps1 -Configuration Debug -BuildParallelism 1` | Passes locally |
| Evidence status and baseline metadata | Current-state audit plus `tools/collect_baseline.ps1` | Implemented |
| Testable runtime ownership | `RuntimeOrchestrator`, `ThreadedRuntimeComponent`, `MonitoringScheduler`, fake-clock tests | Implemented |
| Recovery is not ordinal severity | `RuntimeFoundationTests`, `test_model_contract.py` | Passes |
| Legacy confidence cannot authorize action | `DecisionEngineTests` blocks with `MODEL_CALIBRATION_REQUIRED` | Passes |
| Component-failure handling | Injected collector, inference, storage, policy, and UI failures in `RuntimeFoundationTests` | Passes |
| Versioned contracts and migration safety | v3 schemas, migrations 16-19, dataset/migration tests | Passes |
| Weak-label certification block | `test_dataset_contract.py`, `test_evaluation_pipeline.py`, legacy trainer guard | Passes |
| Adaptive expensive-collector scheduling | `AdaptiveSamplingTests`, live scheduler use | Passes |
| Observer-effect limits | governor, runtime-performance persistence, retention, and tests | Passes |
| Leakage rejection | grouped split and locked-split tests | Passes |
| Hardware/OS baseline metadata | `tools/collect_baseline.ps1` | Implemented |

## External Evidence Still Required

- Run repeatable workload baselines on each intended hardware/Windows support
  slice and retain the resulting evidence.
- Collect controlled-lab or measured-QoE labels before any certification claim.
- Independently reproduce results outside this development machine.

Those are external validation gates, not code-completion claims. Their absence
keeps the product `NOT_CERTIFIED` and does not permit automatic actuation.