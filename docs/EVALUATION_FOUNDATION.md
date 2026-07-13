# Leakage-Safe Evaluation Foundation

Phase 1 evaluation is episode-level. It does not accept row-level telemetry
splits or treat high resource use as user-impact ground truth.

## Required Data Gates

- `python/label_validation.py` rejects contradictory primary outcomes for the
  same episode and rejects synthetic/weak labels from certification input.
- `python/grouped_temporal_split.py` retains every episode in exactly one role
  and can enforce device, workload, application, session, or episode grouping.
- `python/locked_split.py` hashes locked episode IDs and rejects their later use
  in training, validation, or calibration.

## Required Comparisons

`python/baselines.py` exposes majority/no-event, deterministic resource-rule,
persistence, and EWMA research baselines. The deterministic resource rule is a
comparison only; it is never an outcome label, certification signal, or action
authorization input. Logistic regression and monotonic boosted-tree baselines
fail explicitly until the pinned offline ML environment is available.

## Reports

`python/evaluation_metrics.py` reports confusion counts, balanced accuracy,
macro F1, AUROC, AUPRC, Brier score, log loss, calibration error, critical
recall with a Wilson lower bound, and risk-coverage points.
`python/slice_report.py` reports the same episode-level evaluation by device,
workload, and application family.

These mechanics are not a certification result. The legacy training entry point
remains research-only until it delegates to a fully populated v3 episode data
pipeline with independent calibration and locked-test evidence.
