# Current-State Audit

Audit date: 2026-07-13  
Audited commit: `f86d2c9d3595dde9443778a0d6e252beb2d02c63` (`UI Upgrade`)

## Status Vocabulary

- **Runtime-connected**: exercised by the Windows application runtime.
- **Test-verified**: covered by an automated test that passed in this audit.
- **Benchmark-verified**: measured on real hardware using a retained result. No capability has this status unless explicitly stated.
- **Simulated/research-only**: useful for experimentation but not evidence of a real user benefit or production readiness.

## Evidence Collected

| Check | Result |
|---|---|
| CMake Debug build | Passed |
| Native CTest suite | 8 of 8 passed |
| Python suite | 6 of 6 passed |
| Windows host | Windows 11 Home Single Language, build 26200 |
| Reference host | AMD Ryzen 3 3250U, 4 logical processors, 3534 MB RAM |
| Debug application binary | 4,136,960 bytes |

The raw machine/build metadata is recorded by `tools/collect_baseline.ps1`. These results are a local development baseline, not a cross-device benchmark campaign.

## Capability Inventory

| Capability | Current status | Audit evidence | Truthful limitation |
|---|---|---|---|
| System metrics | Runtime-connected; test-verified | `SystemMetrics.*`, `main.cpp`, native test suite | Windows-only and no declared support envelope. |
| QoE telemetry | Runtime-connected; test-verified | `PerformanceIntelligence.*`, `PerformanceIntelligenceTests` | `QoeTelemetryCollector::Capture` is currently called on every one-second monitor loop even when it recommends a longer interval. WM_NULL-style responsiveness is a supporting proxy, not physical input latency. |
| Adaptive baseline / workload detection | Runtime-connected; test-verified | `AdaptiveBaseline.*`, `PerformanceIntelligence.*` | It is not yet a versioned support-envelope or certified drift gate. |
| SQLite telemetry / journals | Runtime-connected; test-verified | `MetricsStorage.*`, QoE and action journals | Schema creation is incremental SQL, not versioned migrations with migration tests. |
| Model inference | Runtime-connected; test-verified | `main.cpp`, `inference_service.py`, `test_model_contract.py` | Runtime starts Python, loads `joblib`, and exchanges polled JSON files. This is research-only and does not meet the native signed inference requirement. |
| Model confidence | Runtime-connected; test-verified | `model_features.py`, `test_model_contract.py` | The displayed confidence is a hand-weighted probability/margin score, not calibrated confidence and must not authorize actions. |
| Model training/evaluation | Implemented; test-verified | `train_model.py`, `model_features.py` | Uses state labels and currently lacks independent episode split, calibration split, locked test controls, and measured QoE primary labels. |
| Decision / diagnosis | Runtime-connected; test-verified | `DecisionEngine.*`, `model_features.py` | Root-cause heuristics are not a causal checksum and may not authorize actions. |
| Reversible process actions | Implemented; test-verified; disabled by default | `ResourceOrchestrator.*`, `ResourceOrchestratorTests` | In-process development executor only; no privileged service boundary or proof-carrying action. |
| Safety / rollback | Runtime-connected; test-verified | `SafetyPolicy.*`, `ActionVerification.*`, `SafeOnlinePolicy.*` | Current safety gates are valuable foundations but not the Phase 3 trusted controller/lease service. |
| Impact learning / policy promotion | Runtime-connected in shadow/research path; test-verified | `ImpactLearning.*`, `SafeOnlinePolicy.*` | No causal counterfactual certificate; impact state must be audited for full persistence in Phase 2. |
| Browser / BITS / prefetch integrations | Implemented; opt-in; test-verified | `CooperativeIntegrations.*`, `BrowserIntegrationBridge.*` | Disabled by default and not yet connected to a Phase 3 proof packet, trusted service, canary, and certificate workflow. |
| Dashboard | Runtime-connected; visually exercised | `ModernDashboardUI.*`, `main.cpp` | Current views primarily display state. Phase 3 must add real guarded command handlers and manual-canary approval. |
| Background agent | Runtime-connected; test-verified | `BackgroundAgent.*`, `StageAutopilotTests` | No production installer/service lifecycle or signed release. |
| Packaging | Implemented | `package_release.ps1` | Produces portable ZIP; no signing, installer, upgrade/uninstall test, SBOM, or update chain. |
| CI | Implemented but incomplete | `.github/workflows/ci.yml` | Does not yet run benchmark-lab tests, formatting/static checks, or a packaging dry run. |
| Benchmark lab | Implemented; test-verified | `benchmark_lab.py`, `test_benchmark_lab.py` | Existing synthetic results are not real benchmark proof and cannot support performance claims. |

## Known-Risk Verification

| Master-plan risk | Audit finding | Required phase |
|---|---|---|
| QoE schedule only changes writes | Confirmed. Capture occurs before `recommendedSampleIntervalMs` is used to gate journal writes. | 1D |
| Hand-written confidence | Confirmed. `model_features.py` computes a weighted top-probability/margin score and the test asserts it. | 1E / 2B |
| Overlapping / unsafe evaluation | Not yet disproven; current training is not episode-group/locked-split based. Treat as unresolved. | 1E |
| Threshold-derived labels | Confirmed risk. Runtime/training state labels and resource thresholds share inputs; no measured QoE certification label contract exists. | 1C / 1E |
| RECOVERY ordinal severity | Requires a named-severity regression audit. Current model still treats it as a primary state label. | 1B |
| Runtime joblib / JSON | Confirmed. Python `joblib` service and JSON input/output are active. | 3C |
| Manual approval workflow | Unresolved. The runtime config is `MANUAL`, but there is no Phase 3 command/confirmation path. | 1B / 3D |
| Impact-state restoration | Unresolved and must be tested before Phase 2 promotion claims. | 2C |
| Policy evaluation/promotion workflow | Partially connected; current research-path implementation needs an independent audit. | 2C |
| Cooperative integrations runtime connection | Partially connected and opt-in; not Phase 3 end-to-end trusted-action connected. | 3E |
| `main.cpp` coupling | Confirmed. `main.cpp` is 2,050 lines and owns monitoring, inference, state, policy, agent, and Win32 UI coordination. | 1B |
| CI coverage | Confirmed incomplete. Benchmark-lab tests and packaging are missing. | 1A |
| Signing/security/installer/certification | Confirmed incomplete. | 3A, 3C, 4B-4E |

## Current Safety Posture

The checked-in configuration keeps execution disabled: `ACTION_EXECUTION_ENABLED=0`, `ACTION_GLOBAL_DISABLE=1`, `ONLINE_POLICY_ENABLED=0`, `ONLINE_POLICY_PROMOTED=0`, and cooperative integrations disabled. This remains required until the four-phase plan has produced valid evidence for a specific support slice.

## Claims Status

The only honest current status is:

> Research prototype / recommendation-only or manual-canary / not certified for autonomous production use.

There is no signed model package, production installer, independent locked certification dataset, real cross-device benchmark campaign, or valid certificate that permits a reliability or performance claim.

## Phase 1A Exit Status

Complete. The audit, CI repair, reproducible local checks, metadata/baseline tooling, and licensing notice are present. The remaining Phase 1 work begins with runtime decomposition and immediate correctness.
