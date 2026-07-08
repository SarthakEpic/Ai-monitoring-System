from __future__ import annotations

import argparse
import sqlite3
from collections import Counter


def has_column(conn: sqlite3.Connection, table: str, column: str) -> bool:
    return any(row[1] == column for row in conn.execute(f"PRAGMA table_info({table});"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Show training-data scenario coverage from monitor.db.")
    parser.add_argument("--db", default="build/Debug/monitor.db")
    args = parser.parse_args()

    conn = sqlite3.connect(args.db)
    try:
        total = conn.execute("SELECT COUNT(*) FROM metrics").fetchone()[0]
        print(f"Rows: {total}")

        if has_column(conn, "metrics", "scenario_label"):
            rows = conn.execute(
                "SELECT UPPER(COALESCE(scenario_label, 'AUTO')), COUNT(*) FROM metrics GROUP BY UPPER(COALESCE(scenario_label, 'AUTO'))"
            ).fetchall()
        else:
            rows = [("AUTO", total)]
            print("scenario_label column not found. Treating existing rows as AUTO.")
    finally:
        conn.close()

    counts = Counter({label: count for label, count in rows})
    for label in ["NORMAL", "WARNING", "CRITICAL", "RECOVERY", "AUTO"]:
        print(f"{label:8} {counts.get(label, 0)}")

    print("\nSuggested minimum before serious retraining:")
    print("NORMAL   300+ rows")
    print("WARNING  300+ rows")
    print("CRITICAL 150+ rows")
    print("RECOVERY 150+ rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
