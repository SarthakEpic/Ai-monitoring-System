# Benchmark Laboratory

## Generate a Plan

```powershell
python benchmark_lab.py plan --output benchmarks\plans\4gb-hdd-01.csv --device-id 4gb-hdd-01 --scenario all --repetitions 10 --seed 20260711
```

Add competitor variants only when they are installed and can be tested with identical conditions:

```powershell
python benchmark_lab.py plan --output benchmarks\plans\comparison.csv --device-id 4gb-hdd-01 --scenario student_browser --variant windows_baseline --variant predictive_autoheal --variant process_lasso --repetitions 10
```

The plan includes cold and warm runs and randomizes variant order with a reproducible seed.

## Record Raw Results

Write one JSON object per completed run using [raw_run.schema.json](benchmarks/raw_run.schema.json). Every metric declares its unit, direction, and raw samples. Keep failures in the file with their safety counters.

The repository contains [benchmark_record_template.json](benchmarks/benchmark_record_template.json) as a shape reference. It contains placeholders, not measured evidence.

## Validate

```powershell
python benchmark_lab.py validate --input benchmarks\raw\campaign.jsonl
```

Validation rejects malformed JSON, duplicate run IDs, unsupported scenarios/variants, empty or non-finite samples, invalid directions, and negative safety counts.

## Generate Reports

```powershell
python benchmark_lab.py report --input benchmarks\raw\campaign.jsonl --output benchmarks\reports\campaign.md --json-output benchmarks\reports\campaign.json --minimum-runs 10 --seed 20260711
```

The report calculates median/p95/p99, paired bootstrap 95% confidence intervals, safety and overhead gates, negative results, and a claim status. It emits `NO_CLAIM` when evidence is insufficient or unsafe.

`QUALIFIED_2X_RESPONSIVENESS` means at least 50% lower p95 for the exact named latency metric with a positive confidence lower bound and all quality gates passed. It never means doubled CPU, RAM, GPU, or universal computer performance.
