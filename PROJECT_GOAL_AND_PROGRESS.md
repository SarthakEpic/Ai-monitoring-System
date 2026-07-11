# Project Goal and Progress

Last updated: 2026-07-11

## Source of Truth

This file is the persistent implementation ledger for PredictiveAutoHeal. Read it before making architectural changes and update it after every milestone. A milestone is complete only when its exit criteria and verification evidence are recorded here.

## Product Mission

Build a safety-first, intent-aware Windows resource orchestrator that learns which reversible resource action improves the current user's real foreground experience, measures the result, and automatically rolls back harmful actions.

The product optimizes responsiveness, latency, stuttering, and interruption. It does not optimize cosmetic free-RAM numbers, promise universal hardware improvements, or treat simulated estimates as proof.

## Non-Negotiable Safety Rules

- The foreground process family, system-critical processes, security software, accessibility software, audio/video workloads, and recently active applications are protected.
- Machine-learning output can advise the deterministic safety controller but cannot bypass it.
- Every real action must be reversible, time-bounded, transactionally recorded, and restorable to the captured original state.
- Unknown or high-uncertainty situations use no intervention, shadow mode, or explicit approval.
- Automatic process killing, arbitrary service disabling, indiscriminate cache clearing, registry cleaning, page-file disabling, Defender disabling, and Windows Update disabling are outside product scope.
- Estimated, planned, executed, verified, harmful, and rolled-back outcomes must never be mixed.
- Real execution remains disabled by default and requires explicit configuration, allowlisting, policy eligibility, and the global kill switch to be armed.
- Performance claims require repeatable measured evidence and must identify the workload, hardware, metric, repetitions, and confidence interval.

## Current Starting Point

Existing and retained foundations:

- Windows CPU, memory, disk, network, process, and foreground-intent monitoring.
- Process Genome classification and foreground/recent-app protection.
- SQLite telemetry, decision audits, dry-run plans, simulated verification, safety policy, adaptive baseline, and runtime health.
- Low-end recommendation-only autopilot, tray agent, and estimated benchmark display.

Known gaps at program start:

- No real action executor is wired into the runtime.
- Existing healing verification and benchmark after-values are estimates.
- No measured foreground quality-of-experience loop exists.
- The current model predicts state labels rather than action impact.
- No constrained contextual action policy exists.
- Runtime Python inference remains present.
- Current training report is research-only and contains no manual labels.
- Cross-device benchmarks, independent reproduction, and public research validation do not yet exist.

## Milestone Ledger

### Milestone 1 - Real Reversible Actuators

Status: COMPLETE

Scope:

- Native Windows priority-class executor.
- Native Windows EcoQoS/power-throttling executor.
- Native Windows memory-priority executor.
- Common action interface and typed action context/outcome.
- Original-state capture, transaction IDs, bounded duration, rollback, process-identity checks, allowlist enforcement, protected-target rejection, and global kill switch.
- Crash-recovery journal for unfinished transactions.
- Execution disabled by default.

Exit criteria:

- Each supported action can be applied to an isolated test process and its original state restored without restarting the process.
- Failure at any step leaves the target unchanged or triggers rollback.
- Unit/integration tests cover protection, allowlisting, invalid PID, apply, rollback, idempotency, and timeout restoration.

Evidence: `ResourceOrchestratorTests` passed on 2026-07-11. Isolated child-process tests cover priority, EcoQoS, and memory-priority apply/restore, global disable, foreground protection, approval, allowlisting, expiry rollback, harmful-outcome rollback, and startup recovery of an unfinished transaction. Runtime execution remains disabled by default.

### Milestone 2 - Measured Outcome Verification

Status: COMPLETE

Scope:

- Pre-action and post-action snapshots.
- Explicit statuses: `ESTIMATED`, `PLANNED`, `EXECUTED`, `VERIFIED_HELPFUL`, `VERIFIED_NEUTRAL`, `VERIFIED_HARMFUL`, `ROLLED_BACK`.
- Real metric deltas and automatic rollback when guardrails regress.
- Rename simulated proof concepts to impact simulation until measured evidence exists.

Exit criteria:

- No estimated value is presented as measured proof.
- Real execution records actual before/after values and a verification status.
- Harmful outcomes automatically invoke rollback and record the rollback result.

Evidence: `ActionVerificationTests` and the integrated resource tests passed. Raw pre/post snapshots and outcomes are persisted; missing evidence remains unmeasured; helpful, neutral, and harmful results are distinct; harmful results trigger rollback. Estimated UI values are labeled Impact Simulation.

### Milestone 3 - Foreground QoE Telemetry

Status: COMPLETE

Scope:

- Low-overhead adaptive telemetry for hard-fault rate, disk contention, foreground CPU progress/wait proxy, input-latency proxy, and workload-specific frame/drop signals where supported.
- Workload-phase detection for launch, active interaction, passive playback, compilation, gaming, meeting, transfer, idle, battery saver, and post-boot stabilization.
- Performance Criticality Graph for foreground dependencies and process relationships.
- Bounded storage and overhead budgets.

Exit criteria:

- Decisions use user-facing performance signals rather than resource percentages alone.
- Telemetry overhead is measured and reported separately.
- Unsupported metrics degrade safely and are marked unavailable.

Evidence: `PerformanceIntelligenceTests` passed. Live capability-detected QoE collection, adaptive sampling, bounded retention, workload phases, and the Performance Criticality Graph are integrated into decision risk and candidate protection.

### Milestone 4 - Shadow Impact Learning

Status: COMPLETE

Scope:

- Workload-context model contract.
- Per-action impact prediction contract.
- Uncertainty estimation and abstention.
- Contextual-bandit policy in shadow mode.
- Offline policy evaluation against a no-intervention baseline.
- Versioned features, policy, model, and reward records.

Exit criteria:

- Candidate actions are scored without execution.
- Logged outcomes can train/evaluate the policy without label leakage.
- Offline lower-confidence benefit must exceed the baseline before policy promotion.

Evidence: `ImpactLearningTests` passed. Native per-action impact learning, uncertainty, lower-confidence abstention, shadow decisions, offline inverse-propensity evaluation, persistent promotion evidence, and privacy-clipped/noised federated-update contracts are implemented.

### Milestone 5 - Safe Online Policy

Status: COMPLETE

Scope:

- Constrained action selection using deterministic safety gates.
- Confidence and lower-bound requirements.
- Maximum action duration, per-target cooldown, action budget, approval mode, global emergency disable, startup recovery, and automatic rollback thresholds.
- Baseline policy remains no intervention.

Exit criteria:

- Only reversible allowlisted actions can become execution eligible.
- Policy cannot bypass the safety shield.
- Kill switch and crash recovery restore all active transactions.
- Harmful or uncertain actions return to baseline.

Evidence: `SafeOnlinePolicyTests` and all six CTest targets passed on 2026-07-11. Online execution requires both execution switches, persisted offline promotion plus the promotion config gate, deterministic execution eligibility, reversible action support, allowlisting, approval, lower-confidence benefit, target safety, criticality limits, cooldown, hourly budget, and absence of the kill-switch file. Isolated tests verify execution, emergency restoration, cooldown, startup recovery, and kill-switch blocking. Defaults remain fully disabled.

### Milestone 6 - Cooperative Integrations

Status: COMPLETE

Scope:

- Chrome/Edge extension contract for safe inactive-tab discard with active, pinned, audible, and media protection.
- User-owned transfer pause/resume adapter.
- IDE, game, meeting, and media workload protection.
- Predictive prefetch interface guarded by confidence and measured value.
- Integrations remain opt-in and capability-detected.

Exit criteria:

- Integrations never terminate arbitrary browser helpers or suspend unidentified system transfers.
- Every cooperative action supports restore/resume and audit.
- Missing integrations do not weaken core safety.

Evidence: `CooperativeIntegrationsTests` and `BrowserIntegrationBridgeTests` passed on 2026-07-11; all eight CTest targets passed. The optional browser extension independently blocks active, pinned, audible, media-sharing, privileged, unapproved, and non-discardable tabs; transactions support reload restoration and Quick Restore. A native messaging relay, install script, pipe bridge, current-user/explicit-approval BITS pause-resume adapter, workload-specific protection engine, confidence-and-history-gated prefetch lease, audit table, capability detection, and correct release packaging are implemented. Integrations default disabled.

### Milestone 7 - Reproducible Benchmark and Research Package

Status: COMPLETE (LOCAL IMPLEMENTATION; EXTERNAL VALIDATION PENDING)

Scope:

- Benchmark harness and schemas for browser/student, coding, meeting, gaming, and post-boot scenarios.
- Randomized baseline/optimizer ordering, cold/warm runs, repetitions, median/p95/p99, confidence intervals, overhead accounting, and negative-result publication.
- Windows Performance Recorder/Analyzer capture guidance and Speedometer integration guidance.
- Architecture, threat model, experimental protocol, dataset schema, failure cases, and research-paper draft.
- Comparison protocol for Windows baseline, Microsoft PC Manager, Process Lasso, and Razer Cortex.

Exit criteria:

- The repository can produce a reproducible local benchmark report from raw run records.
- Claims are generated only from measured data that passes predefined quality gates.
- Cross-device execution, competitor comparison, independent reproduction, paper review, and real-world adoption are recorded as external validation, never claimed from code alone.

Evidence: Verified on 2026-07-12. The seeded planner and strict JSONL validator cover five workload scenarios, cold/warm runs, Windows baseline plus four comparison variants, finite values, duplicate IDs, and metric direction. The report generator produces median/p95/p99 statistics, paired bootstrap 95% confidence intervals, overhead and safety gates, and retained negative results. Six Python tests passed. An end-to-end synthetic campaign validated four records and produced `NO_CLAIM` despite a synthetic 56.1% p95 improvement because non-synthetic, minimum-run, and paired-randomization gates failed. Architecture, threat model, experimental protocol, benchmark guide, failure cases, and research draft are packaged. A clean strict MSVC build completed with zero warnings, all eight native CTest targets passed, PowerShell/JSON/JSONL/browser-JavaScript checks passed, and the portable folder plus ZIP were rebuilt.

## External Validation Gates

These cannot be completed by implementation alone:

- Tests on 4 GB HDD, 4 GB SATA SSD, older 8 GB, integrated-graphics, Windows 10, and Windows 11 systems.
- Independent reproduction by someone outside the development environment.
- Long-duration safety testing and meaningful user adoption.
- Professional patent prior-art and claims review.
- Research-paper acceptance or independent technical review.

## Program Status

Current status: Milestones 1-7 are complete as a locally implemented and tested research prototype. Real action execution remains disabled by default.

Next action: run the documented randomized benchmark campaign on the hardware matrix, retain all negative and safety results, and obtain independent reproduction before making any public performance or production-readiness claim.
