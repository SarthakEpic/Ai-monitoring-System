# Intent-Aware Reversible Micro-Optimization for Low-End Windows PCs

Status: engineering draft. No performance claims or publication acceptance are asserted.

## Abstract

Low-end personal computers often suffer from foreground latency caused by background CPU, paging, disk, and network contention. Existing optimizers commonly apply static process rules or report reclaimed memory. We present a safety-first resource orchestrator that models foreground intent and process criticality, chooses among reversible Windows resource-control micro-actions, measures actual foreground outcomes, rolls back regressions, and learns per-device action value under uncertainty. The system separates a deterministic control plane from a slower contextual learning plane. This draft defines the mechanism and experimental protocol; multi-device results and independent reproduction remain future work.

## Claimed Contributions to Evaluate

1. A foreground Performance Criticality Graph combining process family, recent activity, workload phase, visibility, and measured dependency signals.
2. Transactional reversible process controls with identity-safe restoration, duration limits, startup recovery, and emergency rollback.
3. Per-action contextual impact prediction with uncertainty and a no-intervention lower-confidence baseline.
4. Real-time outcome verification that distinguishes helpful, neutral, harmful, and rolled-back actions.
5. Cooperative browser/BITS/prefetch actions that preserve application-level semantics instead of killing processes.
6. A reproducible benchmark and claim-gating system that retains negative and unsafe results.

These are research hypotheses until novelty review, experiments, and independent comparison are complete.

## System Design

The fast plane captures Windows and QoE telemetry, detects workload phase, builds criticality protection, evaluates deterministic policy, persists original state, applies a bounded action, and verifies/rolls back. The learning plane encodes context, predicts action reward and uncertainty, evaluates a shadow policy offline, and records promotion evidence. Configuration alone cannot promote a policy.

Actions currently include priority class, EcoQoS execution-speed throttling, memory priority, approved inactive-tab discard, current-user BITS pause/resume, and confidence-gated file prefetch. Automatic process termination and permanent system tweaks are excluded.

## Safety Invariants

- The model cannot bypass deterministic policy.
- No mutation occurs before durable pre-action state.
- PID, creation time, and executable path must match at apply and rollback.
- Foreground, recent, critical-path, system/security/accessibility, and audio/video dependencies are protected.
- Every online action has a deadline, cooldown, budget, approval/allowlist, and emergency restore path.
- Missing evidence means abstention, not inferred success.

## Learning Method

Each action has a regularized linear contextual reward model. Prediction exposes expected reward, uncertainty, and a lower confidence bound. The shadow policy selects an action only for evaluation; online eligibility requires sufficient measured observations and persisted offline evidence that the lower-confidence benefit exceeds no intervention. Logged-policy evaluation uses propensity-aware estimates. Fleet update contracts clip deltas, add configurable secure noise, and replace local categories with salted tokens; a production aggregation service is not yet claimed.

## Evaluation Plan

Evaluate five workloads across the hardware matrix in [EXPERIMENTAL_PROTOCOL.md](EXPERIMENTAL_PROTOCOL.md). Primary outcomes are p95/p99 foreground latency, hard-page-read pressure, frame/audio stability, and background completion. Report safety events and optimizer overhead separately. Compare against unmodified Windows and, where reproducible, PC Manager, Process Lasso, and Razer Cortex.

## Required Results Before Submission

- Per-device and aggregate distributions with confidence intervals.
- Ablations for criticality graph, workload phase, uncertainty gate, and rollback.
- False intervention and missed-opportunity analysis.
- Long-duration safety and crash-recovery campaigns.
- Competitor configurations and raw anonymized records.
- Negative results and workload classes where no action helps.
- Independent reproduction and an explicit prior-art section.

## Limitations

Public Windows telemetry provides imperfect causal proxies. Some cooperative integrations require user installation. Device-local learning is initially data-limited. Current automated tests demonstrate mechanism and safety contracts, not real-world speedup. Patentability and research novelty require professional and peer review.
