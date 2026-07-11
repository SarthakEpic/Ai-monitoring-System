import csv
import json
import tempfile
import unittest
from pathlib import Path

import benchmark_lab


def record(run_id, variant, iteration, samples, *, crashes=0, overhead_cpu=0.4, overhead_memory=30.0):
    return {
        "schema_version": 1,
        "run_id": run_id,
        "device_id": "4gb-hdd-01",
        "scenario": "student_browser",
        "variant": variant,
        "run_type": "warm",
        "iteration": iteration,
        "order_index": (1 if iteration % 2 else 2) if variant == "windows_baseline" else (2 if iteration % 2 else 1),
        "metrics": {
            "app_switch_latency_ms": {
                "unit": "ms",
                "direction": "lower",
                "samples": samples,
            }
        },
        "optimizer_overhead": {
            "cpu_percent_samples": [overhead_cpu],
            "memory_mb_samples": [overhead_memory],
            "disk_write_kbps_samples": [2.0],
        },
        "safety": {
            "crashes": crashes,
            "app_reloads": 0,
            "user_apps_touched": 0,
            "rollback_failures": 0,
        },
        "environment": {
            "os_build": "26100",
            "power_mode": "balanced",
            "thermal_state": "stable",
        },
    }


class BenchmarkLabTests(unittest.TestCase):
    def test_randomized_plan_is_reproducible_and_balanced(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.csv"
            second = Path(directory) / "second.csv"
            benchmark_lab.create_plan(first, "device", ["coding"],
                                      ["windows_baseline", "predictive_autoheal"], 5, 42)
            benchmark_lab.create_plan(second, "device", ["coding"],
                                      ["windows_baseline", "predictive_autoheal"], 5, 42)
            self.assertEqual(first.read_text(), second.read_text())
            with first.open(newline="", encoding="utf-8") as source:
                variants = [row["variant"] for row in csv.DictReader(source)]
            self.assertEqual(variants.count("windows_baseline"), 10)
            self.assertEqual(variants.count("predictive_autoheal"), 10)

    def test_report_qualifies_only_measured_safe_improvement(self):
        runs = []
        for iteration in range(1, 11):
            runs.extend(benchmark_lab.validate_record(record(
                f"base-{iteration}", "windows_baseline", iteration, [100, 110, 120, 130, 140]
            )))
            runs.extend(benchmark_lab.validate_record(record(
                f"opt-{iteration}", "predictive_autoheal", iteration, [35, 40, 45, 50, 55]
            )))
        report = benchmark_lab.build_report(runs, 10, 123)
        comparison = report["comparisons"][0]
        self.assertEqual(comparison["claim_status"], "QUALIFIED_2X_RESPONSIVENESS")
        self.assertTrue(comparison["quality"]["passed"])
        self.assertGreater(comparison["bootstrap_95_ci"][0], 0)

    def test_safety_regression_blocks_claim(self):
        runs = []
        for iteration in range(1, 11):
            runs.extend(benchmark_lab.validate_record(record(
                f"base-{iteration}", "windows_baseline", iteration, [100, 110, 120]
            )))
            runs.extend(benchmark_lab.validate_record(record(
                f"opt-{iteration}", "predictive_autoheal", iteration, [30, 35, 40], crashes=1 if iteration == 1 else 0
            )))
        report = benchmark_lab.build_report(runs, 10, 123)
        self.assertEqual(report["comparisons"][0]["claim_status"], "NO_CLAIM")
        self.assertFalse(report["comparisons"][0]["quality"]["checks"]["zero_safety_regressions"])
        self.assertTrue(report["negative_results"])

    def test_invalid_or_duplicate_records_fail(self):
        broken = record("one", "predictive_autoheal", 1, [])
        with self.assertRaises(benchmark_lab.ValidationError):
            benchmark_lab.validate_record(broken)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runs.jsonl"
            valid = record("duplicate", "windows_baseline", 1, [1])
            path.write_text(json.dumps(valid) + "\n" + json.dumps(valid) + "\n")
            with self.assertRaises(benchmark_lab.ValidationError):
                benchmark_lab.load_runs(path)


if __name__ == "__main__":
    unittest.main()
