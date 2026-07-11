#!/usr/bin/env python3
"""Reproducible benchmark planning, validation, and measured report generation."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
PLAN_VERSION = 1
SCENARIOS = (
    "student_browser",
    "coding",
    "video_meeting",
    "gaming",
    "post_boot",
)
BASELINE_VARIANT = "windows_baseline"
KNOWN_VARIANTS = {
    BASELINE_VARIANT,
    "predictive_autoheal",
    "microsoft_pc_manager",
    "process_lasso",
    "razer_cortex",
}


class ValidationError(ValueError):
    pass


@dataclass(frozen=True)
class MetricRun:
    run_id: str
    device_id: str
    scenario: str
    variant: str
    run_type: str
    iteration: int
    order_index: int
    metric_name: str
    unit: str
    direction: str
    samples: tuple[float, ...]
    overhead_cpu: tuple[float, ...]
    overhead_memory: tuple[float, ...]
    overhead_disk: tuple[float, ...]
    crashes: int
    app_reloads: int
    user_apps_touched: int
    rollback_failures: int
    environment: dict[str, Any]
    notes: str


def _finite_number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValidationError(f"{field} must be numeric")
    number = float(value)
    if not math.isfinite(number):
        raise ValidationError(f"{field} must be finite")
    return number


def _number_list(value: Any, field: str, *, allow_empty: bool = False) -> tuple[float, ...]:
    if not isinstance(value, list) or (not value and not allow_empty):
        requirement = "a list" if allow_empty else "a non-empty list"
        raise ValidationError(f"{field} must be {requirement}")
    return tuple(_finite_number(item, f"{field}[{index}]") for index, item in enumerate(value))


def _nonnegative_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValidationError(f"{field} must be a non-negative integer")
    return value


def validate_record(record: Any, line_number: int = 0) -> list[MetricRun]:
    prefix = f"line {line_number}: " if line_number else ""
    try:
        if not isinstance(record, dict):
            raise ValidationError("record must be an object")
        if record.get("schema_version") != SCHEMA_VERSION:
            raise ValidationError(f"schema_version must be {SCHEMA_VERSION}")
        required_strings = ("run_id", "device_id", "scenario", "variant", "run_type")
        for field in required_strings:
            if not isinstance(record.get(field), str) or not record[field].strip():
                raise ValidationError(f"{field} must be a non-empty string")
        if record["scenario"] not in SCENARIOS:
            raise ValidationError(f"unsupported scenario {record['scenario']!r}")
        if record["variant"] not in KNOWN_VARIANTS:
            raise ValidationError(f"unsupported variant {record['variant']!r}")
        if record["run_type"] not in {"cold", "warm"}:
            raise ValidationError("run_type must be cold or warm")
        iteration = _nonnegative_int(record.get("iteration"), "iteration")
        order_index = _nonnegative_int(record.get("order_index"), "order_index")
        metrics = record.get("metrics")
        if not isinstance(metrics, dict) or not metrics:
            raise ValidationError("metrics must be a non-empty object")
        overhead = record.get("optimizer_overhead", {})
        if not isinstance(overhead, dict):
            raise ValidationError("optimizer_overhead must be an object")
        safety = record.get("safety", {})
        if not isinstance(safety, dict):
            raise ValidationError("safety must be an object")
        environment = record.get("environment", {})
        if not isinstance(environment, dict):
            raise ValidationError("environment must be an object")

        common = dict(
            run_id=record["run_id"],
            device_id=record["device_id"],
            scenario=record["scenario"],
            variant=record["variant"],
            run_type=record["run_type"],
            iteration=iteration,
            order_index=order_index,
            overhead_cpu=_number_list(overhead.get("cpu_percent_samples", []), "optimizer_overhead.cpu_percent_samples", allow_empty=True),
            overhead_memory=_number_list(overhead.get("memory_mb_samples", []), "optimizer_overhead.memory_mb_samples", allow_empty=True),
            overhead_disk=_number_list(overhead.get("disk_write_kbps_samples", []), "optimizer_overhead.disk_write_kbps_samples", allow_empty=True),
            crashes=_nonnegative_int(safety.get("crashes", 0), "safety.crashes"),
            app_reloads=_nonnegative_int(safety.get("app_reloads", 0), "safety.app_reloads"),
            user_apps_touched=_nonnegative_int(safety.get("user_apps_touched", 0), "safety.user_apps_touched"),
            rollback_failures=_nonnegative_int(safety.get("rollback_failures", 0), "safety.rollback_failures"),
            environment=environment,
            notes=str(record.get("notes", "")),
        )
        result: list[MetricRun] = []
        for metric_name, metric in metrics.items():
            if not isinstance(metric_name, str) or not metric_name:
                raise ValidationError("metric names must be non-empty strings")
            if not isinstance(metric, dict):
                raise ValidationError(f"metrics.{metric_name} must be an object")
            unit = metric.get("unit")
            direction = metric.get("direction")
            if not isinstance(unit, str) or not unit:
                raise ValidationError(f"metrics.{metric_name}.unit must be non-empty")
            if direction not in {"lower", "higher"}:
                raise ValidationError(f"metrics.{metric_name}.direction must be lower or higher")
            result.append(MetricRun(
                **common,
                metric_name=metric_name,
                unit=unit,
                direction=direction,
                samples=_number_list(metric.get("samples"), f"metrics.{metric_name}.samples"),
            ))
        return result
    except ValidationError as error:
        raise ValidationError(prefix + str(error)) from error


def load_runs(path: Path) -> list[MetricRun]:
    runs: list[MetricRun] = []
    seen_run_ids: set[str] = set()
    with path.open("r", encoding="utf-8") as source:
        for line_number, raw_line in enumerate(source, 1):
            if not raw_line.strip():
                continue
            try:
                record = json.loads(raw_line)
            except json.JSONDecodeError as error:
                raise ValidationError(f"line {line_number}: invalid JSON: {error.msg}") from error
            record_runs = validate_record(record, line_number)
            run_id = record_runs[0].run_id
            if run_id in seen_run_ids:
                raise ValidationError(f"line {line_number}: duplicate run_id {run_id!r}")
            seen_run_ids.add(run_id)
            runs.extend(record_runs)
    if not runs:
        raise ValidationError("input contains no benchmark records")
    return runs


def percentile(values: Iterable[float], probability: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValidationError("cannot compute percentile of an empty collection")
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def summarize(values: Iterable[float]) -> dict[str, float | int]:
    materialized = list(values)
    return {
        "count": len(materialized),
        "median": percentile(materialized, 0.50),
        "p95": percentile(materialized, 0.95),
        "p99": percentile(materialized, 0.99),
        "mean": statistics.fmean(materialized),
    }


def _run_p95(run: MetricRun) -> float:
    return percentile(run.samples, 0.95)


def relative_improvement(baseline: float, candidate: float, direction: str) -> float:
    if baseline == 0:
        return 0.0
    return (baseline - candidate) / abs(baseline) if direction == "lower" else (candidate - baseline) / abs(baseline)


def bootstrap_improvement(
    baseline_runs: list[MetricRun],
    candidate_runs: list[MetricRun],
    direction: str,
    seed: int,
    samples: int = 2000,
) -> tuple[float, float]:
    baseline_by_iteration = {(run.iteration, run.run_type): _run_p95(run) for run in baseline_runs}
    candidate_by_iteration = {(run.iteration, run.run_type): _run_p95(run) for run in candidate_runs}
    paired = sorted(set(baseline_by_iteration) & set(candidate_by_iteration))
    if not paired:
        return (float("nan"), float("nan"))
    rng = random.Random(seed)
    estimates: list[float] = []
    for _ in range(samples):
        selected = [rng.choice(paired) for _ in paired]
        baseline = statistics.fmean(baseline_by_iteration[key] for key in selected)
        candidate = statistics.fmean(candidate_by_iteration[key] for key in selected)
        estimates.append(relative_improvement(baseline, candidate, direction))
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def _flatten_samples(runs: list[MetricRun]) -> list[float]:
    return [sample for run in runs for sample in run.samples]


def _mean_optional(values: Iterable[float]) -> float | None:
    materialized = list(values)
    return statistics.fmean(materialized) if materialized else None


def _quality_checks(baseline: list[MetricRun], candidate: list[MetricRun], minimum_runs: int) -> dict[str, Any]:
    baseline_keys = {(run.iteration, run.run_type) for run in baseline}
    candidate_keys = {(run.iteration, run.run_type) for run in candidate}
    paired = baseline_keys & candidate_keys
    candidate_unique = {(run.run_id, run.iteration, run.run_type) for run in candidate}
    baseline_unique = {(run.run_id, run.iteration, run.run_type) for run in baseline}
    safety = {
        "crashes": max((run.crashes for run in candidate), default=0),
        "app_reloads": max((run.app_reloads for run in candidate), default=0),
        "user_apps_touched": max((run.user_apps_touched for run in candidate), default=0),
        "rollback_failures": max((run.rollback_failures for run in candidate), default=0),
    }
    cpu_overhead = _mean_optional(value for run in candidate for value in run.overhead_cpu)
    memory_overhead = _mean_optional(value for run in candidate for value in run.overhead_memory)
    disk_overhead = _mean_optional(value for run in candidate for value in run.overhead_disk)
    order_pairs: dict[tuple[int, str], dict[str, int]] = defaultdict(dict)
    for run in baseline + candidate:
        order_pairs[(run.iteration, run.run_type)][run.variant] = run.order_index
    distinct_order_per_pair = all(
        len(orders) >= 2 and len(set(orders.values())) == len(orders)
        for orders in order_pairs.values()
        if BASELINE_VARIANT in orders
    )
    candidate_order_positions = {run.order_index for run in candidate}
    order_randomized = distinct_order_per_pair and len(candidate_order_positions) >= 2
    environment_keys = ("os_build", "power_mode", "thermal_state")
    environment_consistent = all(
        len({str(run.environment.get(key, "UNKNOWN")) for run in baseline + candidate}) <= 1
        for key in environment_keys
    )
    non_synthetic_data = all("synthetic" not in run.notes.lower() for run in baseline + candidate)
    checks = {
        "minimum_runs": len(baseline_unique) >= minimum_runs and len(candidate_unique) >= minimum_runs,
        "paired_randomized_runs": len(paired) >= minimum_runs,
        "order_randomized": order_randomized,
        "environment_consistent": environment_consistent,
        "non_synthetic_data": non_synthetic_data,
        "zero_safety_regressions": all(value == 0 for value in safety.values()),
        "cpu_overhead_within_1_percent": cpu_overhead is not None and cpu_overhead <= 1.0,
        "memory_overhead_within_60_mb": memory_overhead is not None and memory_overhead <= 60.0,
    }
    return {
        "checks": checks,
        "passed": all(checks.values()),
        "safety": safety,
        "overhead": {
            "mean_cpu_percent": cpu_overhead,
            "mean_memory_mb": memory_overhead,
            "mean_disk_write_kbps": disk_overhead,
        },
        "paired_runs": len(paired),
    }


def build_report(runs: list[MetricRun], minimum_runs: int, seed: int) -> dict[str, Any]:
    groups: dict[tuple[str, str, str, str, str], list[MetricRun]] = defaultdict(list)
    for run in runs:
        groups[(run.device_id, run.scenario, run.run_type, run.metric_name, run.variant)].append(run)
    comparisons: list[dict[str, Any]] = []
    negative_results: list[str] = []
    base_keys = sorted({key[:4] for key in groups if key[4] == BASELINE_VARIANT})
    for device_id, scenario, run_type, metric_name in base_keys:
        baseline = groups[(device_id, scenario, run_type, metric_name, BASELINE_VARIANT)]
        baseline_units = {run.unit for run in baseline}
        baseline_directions = {run.direction for run in baseline}
        if len(baseline_units) != 1 or len(baseline_directions) != 1:
            raise ValidationError(f"inconsistent baseline metric contract for {device_id}/{scenario}/{metric_name}")
        unit = next(iter(baseline_units))
        direction = next(iter(baseline_directions))
        variants = sorted(key[4] for key in groups if key[:4] == (device_id, scenario, run_type, metric_name) and key[4] != BASELINE_VARIANT)
        for variant in variants:
            candidate = groups[(device_id, scenario, run_type, metric_name, variant)]
            if {run.unit for run in candidate} != {unit} or {run.direction for run in candidate} != {direction}:
                raise ValidationError(f"candidate metric contract differs for {variant}/{scenario}/{metric_name}")
            baseline_summary = summarize(_flatten_samples(baseline))
            candidate_summary = summarize(_flatten_samples(candidate))
            improvement = relative_improvement(float(baseline_summary["p95"]), float(candidate_summary["p95"]), direction)
            ci_low, ci_high = bootstrap_improvement(baseline, candidate, direction, seed)
            quality = _quality_checks(baseline, candidate, minimum_runs)
            claim = "NO_CLAIM"
            if quality["passed"] and math.isfinite(ci_low) and ci_low > 0:
                claim = "QUALIFIED_2X_RESPONSIVENESS" if improvement >= 0.50 else "QUALIFIED_IMPROVEMENT"
            if claim == "NO_CLAIM":
                negative_results.append(
                    f"{device_id}/{scenario}/{run_type}/{metric_name}/{variant}: no qualified improvement claim"
                )
            comparisons.append({
                "device_id": device_id,
                "scenario": scenario,
                "run_type": run_type,
                "metric_name": metric_name,
                "unit": unit,
                "direction": direction,
                "baseline": baseline_summary,
                "candidate_variant": variant,
                "candidate": candidate_summary,
                "p95_relative_improvement": improvement,
                "bootstrap_95_ci": [ci_low, ci_high],
                "quality": quality,
                "claim_status": claim,
            })
    if not comparisons:
        raise ValidationError("no baseline/candidate metric pairs were found")
    return {
        "report_schema_version": 1,
        "raw_record_count": len({run.run_id for run in runs}),
        "minimum_runs_per_variant": minimum_runs,
        "bootstrap_seed": seed,
        "comparisons": comparisons,
        "negative_results": negative_results,
        "disclosures": [
            "Results are workload-, device-, metric-, and run-type-specific.",
            "A 2x claim means at least 50% lower p95 latency for the named metric; it is not a claim of doubled hardware power.",
            "NO_CLAIM results and safety regressions are retained rather than hidden.",
            "External reproduction is required before public product claims.",
        ],
    }


def markdown_report(report: dict[str, Any]) -> str:
    lines = [
        "# PredictiveAutoHeal Measured Benchmark Report",
        "",
        f"Raw runs: {report['raw_record_count']}",
        f"Minimum runs per variant: {report['minimum_runs_per_variant']}",
        "",
        "| Device | Scenario | Type | Metric | Candidate | Baseline p95 | Candidate p95 | Improvement | 95% CI | Claim |",
        "|---|---|---|---|---|---:|---:|---:|---:|---|",
    ]
    for item in report["comparisons"]:
        improvement = item["p95_relative_improvement"] * 100
        low, high = (value * 100 for value in item["bootstrap_95_ci"])
        lines.append(
            f"| {item['device_id']} | {item['scenario']} | {item['run_type']} | {item['metric_name']} ({item['unit']}) "
            f"| {item['candidate_variant']} | {item['baseline']['p95']:.3f} | {item['candidate']['p95']:.3f} "
            f"| {improvement:.1f}% | [{low:.1f}%, {high:.1f}%] | {item['claim_status']} |"
        )
        quality = item["quality"]
        lines.extend([
            "",
            f"Quality gates for `{item['device_id']}/{item['scenario']}/{item['metric_name']}/{item['candidate_variant']}`:",
            "",
        ])
        for check, passed in quality["checks"].items():
            lines.append(f"- {'PASS' if passed else 'FAIL'}: {check}")
        lines.append(f"- Safety: `{json.dumps(quality['safety'], sort_keys=True)}`")
        lines.append(f"- Overhead: `{json.dumps(quality['overhead'], sort_keys=True)}`")
    lines.extend(["", "## Negative Results", ""])
    lines.extend(f"- {result}" for result in report["negative_results"] or ["None in this dataset."])
    lines.extend(["", "## Disclosures", ""])
    lines.extend(f"- {disclosure}" for disclosure in report["disclosures"])
    lines.append("")
    return "\n".join(lines)


def create_plan(output: Path, device_id: str, scenarios: list[str], variants: list[str], repetitions: int, seed: int) -> None:
    if repetitions < 2:
        raise ValidationError("repetitions must be at least 2")
    if BASELINE_VARIANT not in variants or "predictive_autoheal" not in variants:
        raise ValidationError("plan must include windows_baseline and predictive_autoheal")
    unknown = sorted(set(variants) - KNOWN_VARIANTS)
    if unknown:
        raise ValidationError(f"unknown variants: {', '.join(unknown)}")
    rng = random.Random(seed)
    rows: list[dict[str, Any]] = []
    for scenario in scenarios:
        if scenario not in SCENARIOS:
            raise ValidationError(f"unsupported scenario {scenario!r}")
        for run_type in ("cold", "warm"):
            for iteration in range(1, repetitions + 1):
                order = list(variants)
                rng.shuffle(order)
                for order_index, variant in enumerate(order, 1):
                    rows.append({
                        "plan_version": PLAN_VERSION,
                        "run_id": f"{device_id}-{scenario}-{run_type}-{iteration:03d}-{variant}",
                        "seed": seed,
                        "device_id": device_id,
                        "scenario": scenario,
                        "variant": variant,
                        "run_type": run_type,
                        "iteration": iteration,
                        "order_index": order_index,
                        "status": "PLANNED",
                    })
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.DictWriter(destination, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    plan = subparsers.add_parser("plan", help="generate a seeded randomized benchmark plan")
    plan.add_argument("--output", type=Path, required=True)
    plan.add_argument("--device-id", required=True)
    plan.add_argument("--scenario", action="append", choices=(*SCENARIOS, "all"), default=[])
    plan.add_argument("--variant", action="append", choices=sorted(KNOWN_VARIANTS), default=[])
    plan.add_argument("--repetitions", type=int, default=10)
    plan.add_argument("--seed", type=int, default=20260711)
    validate = subparsers.add_parser("validate", help="validate raw JSONL records")
    validate.add_argument("--input", type=Path, required=True)
    report = subparsers.add_parser("report", help="generate measured JSON and Markdown reports")
    report.add_argument("--input", type=Path, required=True)
    report.add_argument("--output", type=Path, required=True)
    report.add_argument("--json-output", type=Path)
    report.add_argument("--minimum-runs", type=int, default=10)
    report.add_argument("--seed", type=int, default=20260711)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    try:
        if args.command == "plan":
            scenarios = list(SCENARIOS) if "all" in args.scenario or not args.scenario else args.scenario
            variants = args.variant or [BASELINE_VARIANT, "predictive_autoheal"]
            create_plan(args.output, args.device_id, scenarios, variants, args.repetitions, args.seed)
            print(f"Wrote randomized plan to {args.output}")
        elif args.command == "validate":
            runs = load_runs(args.input)
            print(f"Valid: {len({run.run_id for run in runs})} records, {len(runs)} metric series")
        elif args.command == "report":
            report = build_report(load_runs(args.input), max(2, args.minimum_runs), args.seed)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(markdown_report(report), encoding="utf-8")
            json_output = args.json_output or args.output.with_suffix(".json")
            json_output.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
            print(f"Wrote measured report to {args.output} and {json_output}")
        return 0
    except (OSError, ValidationError) as error:
        print(f"benchmark_lab: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
