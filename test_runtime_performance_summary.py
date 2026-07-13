from __future__ import annotations

import json
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCRIPT = ROOT / "runtime_performance_summary.py"


class RuntimePerformanceSummaryTests(unittest.TestCase):
    def run_summary(self, database: Path) -> dict:
        completed = subprocess.run(
            [sys.executable, str(SCRIPT), "--db", str(database)],
            check=True,
            capture_output=True,
            text=True,
        )
        return json.loads(completed.stdout)

    def test_missing_database_is_explicitly_unavailable(self) -> None:
        result = self.run_summary(Path(tempfile.gettempdir()) / "missing-aegis-monitor.db")
        self.assertEqual(result["status"], "not_available")
        self.assertIn("does not exist", result["reason"])

    def test_latest_runtime_sample_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            database = Path(temporary_directory) / "monitor.db"
            with sqlite3.connect(database) as connection:
                connection.execute(
                    "CREATE TABLE runtime_performance_samples("
                    "observed_at INTEGER PRIMARY KEY, collector_p95_ms REAL, inference_p95_ms REAL, "
                    "process_cpu_percent REAL, process_io_bytes_per_second REAL, wakeups_per_second REAL, "
                    "working_set_mb REAL, storage_batch_latency_ms REAL, pending_writes INTEGER, observer_state TEXT)"
                )
                connection.execute(
                    "INSERT INTO runtime_performance_samples VALUES(100, 1.2, 3.4, 0.5, 120.0, 1.0, 42.0, 5.0, 2, 'within_budget')"
                )
                connection.execute(
                    "INSERT INTO runtime_performance_samples VALUES(200, 2.2, 4.4, 0.6, 140.0, 1.1, 44.0, 6.0, 3, 'io_budget')"
                )

            connection.close()
            result = self.run_summary(database)

        self.assertEqual(result["status"], "available")
        self.assertEqual(result["schema_version"], 1)
        self.assertEqual(result["metrics"]["observed_at"], 200)
        self.assertEqual(result["metrics"]["observer_state"], "io_budget")
        self.assertEqual(result["metrics"]["storage_batch_latency_ms"], 6.0)


if __name__ == "__main__":
    unittest.main()