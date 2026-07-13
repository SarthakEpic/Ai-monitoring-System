# Phase 2 Acceptance

## Local implementation evidence

- `ReliabilityCascade` routes stable telemetry through a cheap sentinel and routes possible events to specialist and temporal analysis; raw confidence cannot authorize a recommendation.
- Calibration, conformal prediction, OOD/drift monitoring, risk-coverage reports, and artifact hashes are implemented in the Phase 2 Python modules.
- `selective_risk_certificate.py` now uses one-sided exact Clopper-Pearson bounds for autonomy/certification decisions; Wilson-style approximations are not used for the release gate.
- `ReliabilityGate`, `CausalChecksum`, `OfflinePolicyEvaluator`, persisted learning state, and `ReliabilityBudgetCompiler` fail closed for absent or invalid reliability evidence.

## Deliberate non-claims

Phase 2 does not issue a production certificate, enable auto-healing, claim multi-device generalization, or replace runtime inference with a deployed signed native ONNX bundle. Those require later-phase external evidence.

## Required verification

Run `./tools/run_all_checks.ps1 -Configuration Debug -BuildParallelism 1`. It includes exact-binomial, calibration/OOD/drift, cascade, causal, budget, persistence, and Phase 4 certification-contract tests.
