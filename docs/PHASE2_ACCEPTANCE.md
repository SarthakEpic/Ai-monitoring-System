# Phase 2 Acceptance

## Local implementation evidence

- The `ReliabilityCascade` routes stable telemetry through a cheap sentinel and routes possible events to specialist and temporal analysis. It cannot accept a recommendation from raw confidence alone.
- `python/probability_calibration.py`, `conformal_runtime.py`, `selective_risk_certificate.py`, `ood_ensemble.py`, `drift_monitor.py`, `risk_coverage_report.py`, and `model_registry.py` provide calibration-only fitting, predictive sets, finite-sample bounds, OOD/drift downgrades, reports, and artifact hashes.
- `ReliabilityGate` is fail-closed for missing/expired/mismatched certificates, unsupported envelopes, bad data quality, OOD, drift, error budgets, or coverage lower bounds.
- `CausalChecksum` requires a measurable mechanism and complete telemetry before a reversible recommendation can be supported.
- `OfflinePolicyEvaluator` requires observed outcomes, valid logging propensities, adequate effective sample size, causal support, and a positive lower confidence benefit. `LearningJournal` restores validated matrices, reward vectors, and observation counts from SQLite.
- `ReliabilityBudgetCompiler` accepts only measured component upper bounds that fit the global error budget.

## Deliberate non-claims

This phase does **not** issue a production certificate, enable auto-healing, claim multi-device generalization, or replace the legacy runtime Python model with signed native inference. Those require locked external evidence and later phases.

## Required verification

Run `./tools/run_all_checks.ps1 -Configuration Debug -BuildParallelism 1`. The suite includes cascade, causal checksum, reliability gate, budget, persisted learning state, and Python calibration/OOD/drift tests.