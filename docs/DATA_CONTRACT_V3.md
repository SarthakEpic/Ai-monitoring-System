# Aegis-99 Data Contract v3

## Purpose

Version 3 separates measured user impact from resource-pressure heuristics. Its primary label is either `USER_IMPACT_EVENT` or `NO_USER_IMPACT_EVENT`. Root cause, recovery, state labels, and resource pressure are secondary annotations and are never an ordinal substitute for the primary outcome.

## Required Identity and Privacy Fields

Every telemetry, episode, label, and action-outcome record carries a schema version, monotonic and wall-clock time, pseudonymous device ID, session ID, independent episode ID, Windows build family, hardware tier, collector health, and feature-source versions. Raw hardware serials, user document names, full command lines, and telemetry upload are outside this contract.

## Provenance and Certification

`SYNTHETIC` and `WEAK_HEURISTIC` records may exercise mechanics or exploratory pretraining only. They are rejected from certification datasets. Certification eligibility requires `CONTROLLED_LAB`, `MANUAL_REVIEW`, or `MEASURED_QOE` provenance, with measured QoE evidence retained on the label or action outcome.

## Resource and QoE Evidence

Telemetry retains both raw and robust-normalized resource values. Baseline normalization must use device/workload-relative median, MAD, and percentile methods. Supporting QoE signals may include message-loop responsiveness, foreground progress, page reads/faults, disk latency, bounded ETW summaries, frame metrics, and workload progress. A message-loop probe is not physical input latency; unavailable ETW or PresentMon data must remain unavailable, never synthesized.

## Episode Isolation

An episode records workload class, start/end reason, stable baseline, observation interval, outcome interval, and data-quality status. Every episode is the atomic split unit: no training, validation, calibration, or locked-test split may contain overlapping windows from the same episode.

## Schemas and Validation

- `schemas/telemetry_frame.schema.json`
- `schemas/workload_episode.schema.json`
- `schemas/outcome_label.schema.json`
- `schemas/action_outcome.schema.json`
- `python/dataset_schema_v3.py`

The Python validator is deliberately dependency-free and is exercised by `test_dataset_contract.py`. JSON Schema files remain portable documentation and interchange contracts; production ingestion must call the validator before data enters training or certification workflows.

## Evaluation

Leakage checks, label-provenance gates, baseline comparisons, episode metrics, calibration/risk-coverage reporting, and device/workload/application slices are defined in `docs/EVALUATION_FOUNDATION.md` and exercised by `test_evaluation_pipeline.py`.
