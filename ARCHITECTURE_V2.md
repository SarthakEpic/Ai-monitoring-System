# Intent-Aware Adaptive Resource Orchestrator

## Mission

PredictiveAutoHeal optimizes user-perceived responsiveness on constrained Windows computers. It observes foreground intent and resource contention, proposes a reversible micro-action, applies it only through deterministic safety gates, measures the real outcome, restores harmful changes, and learns action value for that device and workload.

It does not treat free RAM as the objective and does not claim that software increases physical hardware capacity.

## Two-Plane Architecture

```text
Fast deterministic control plane
  Windows telemetry -> workload phase -> criticality graph -> safety shield
  -> transactional actuator -> QoE verifier -> keep temporarily / rollback

Slow learning plane
  measured action outcomes -> context encoder -> per-action impact model
  -> uncertainty + lower confidence bound -> shadow policy
  -> offline evaluation -> persisted promotion evidence
```

The learning plane cannot call a Windows actuator directly. The online controller requires both execution switches, persisted policy promotion, deterministic execution eligibility, explicit allowlisting/approval, a reversible executor, action budget, cooldown, criticality threshold, and absence of the emergency kill-switch file.

## Runtime Flow

1. `WindowsMetricsCollector` captures system, process, and foreground intent state.
2. `QoeTelemetryCollector` adds page-read pressure, disk queue, input-response proxy, process progress, and frame/drop capability signals.
3. `WorkloadPhaseDetector` classifies launch, interaction, playback, compilation, gaming, meeting, transfer, idle, battery, or post-boot phases.
4. `PerformanceCriticalityEngine` protects the foreground family and observed workload dependencies.
5. `DecisionEngine` diagnoses pressure and chooses a non-protected candidate.
6. `ContextualImpactModel` predicts per-action reward and uncertainty. `ShadowContextualPolicy` defaults to no intervention.
7. `SafeOnlinePolicyController` applies every deterministic and learned safety gate.
8. `ActionCoordinator` captures the original process identity/state and persists a transaction before mutation.
9. Native priority, EcoQoS, or memory-priority action executes for a bounded period.
10. `MeasuredActionSession` records actual pre/post metrics and labels the result helpful, neutral, or harmful.
11. Harmful outcomes and Quick Restore invoke rollback. Measured rewards update only the matching action model.

## Cooperative Integrations

- Browser integration discards only user-approved inactive web tabs after independent extension-side revalidation. Restore reloads the exact transaction tab.
- BITS integration can pause only an explicitly approved job found in current-user enumeration and resumes it by transaction ID.
- Predictive prefetch maps only an approved file under a byte budget when confidence and measured historical benefit pass their gates; releasing the lease removes mapped resources.
- Missing integrations never reduce process or online-policy safety.

## Persistent Evidence

SQLite stores action transactions, raw action snapshots, measured outcomes, QoE samples, criticality nodes, shadow decisions, offline evaluations, policy versions, rewards, cooperative audits, and privacy-safe federated update audits. Transaction and model/policy version IDs make decisions reviewable.

## Current Boundary

All native mechanisms and local tests are implemented. Public performance claims remain blocked until multi-device benchmark data, long-duration safety evidence, competitor comparison, and independent reproduction exist.
