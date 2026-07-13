"""Read the latest bounded runtime-performance record for local baseline evidence."""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any

COLUMNS = (
    "observed_at",
    "collector_p95_ms",
    "inference_p95_ms",
    "process_cpu_percent",
    "process_io_bytes_per_second",
    "wakeups_per_second",
    "working_set_mb",
    "storage_batch_latency_ms",
    "pending_writes",
    "observer_state",
)


def read_latest(database_path: Path) -> dict[str, Any]:
    if not database_path.is_file():
        return {
            "status": "not_available",
            "reason": "runtime database does not exist",
            "database": str(database_path),
        }

    try:
        with sqlite3.connect(database_path) as connection:
            row = connection.execute(
                "SELECT " + ", ".join(COLUMNS) + " "
                "FROM runtime_performance_samples "
                "ORDER BY observed_at DESC LIMIT 1"
            ).fetchone()
    except sqlite3.Error as error:
        return {
            "status": "not_available",
            "reason": f"runtime performance query failed: {error}",
            "database": str(database_path),
        }

    if row is None:
        return {
            "status": "not_available",
            "reason": "no runtime performance sample has been persisted yet",
            "database": str(database_path),
        }

    return {
        "status": "available",
        "schema_version": 1,
        "database": str(database_path),
        "metrics": dict(zip(COLUMNS, row, strict=True)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", required=True, type=Path, help="Path to monitor.db")
    arguments = parser.parse_args()
    print(json.dumps(read_latest(arguments.db), sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())