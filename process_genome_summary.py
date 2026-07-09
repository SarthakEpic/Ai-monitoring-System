from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path


def _table_exists(conn: sqlite3.Connection, table: str) -> bool:
    row = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
        (table,),
    ).fetchone()
    return row is not None


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect collected Process Genome samples.")
    parser.add_argument("--db", default="build/Debug/monitor.db")
    parser.add_argument("--limit", type=int, default=12)
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app first, then run this summary again.")
        return 0

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    try:
        if not _table_exists(conn, "process_samples"):
            print("process_samples table not found. Run the rebuilt app first.")
            return 0

        total = conn.execute("SELECT COUNT(*) AS count FROM process_samples").fetchone()["count"]
        print(f"Process samples: {total}")
        if total == 0:
            print("No process samples yet. Run the app for at least 30-60 seconds.")
            return 0

        print("\nBy category and safety:")
        rows = conn.execute(
            """
            SELECT category, safety, COUNT(*) AS count
            FROM process_samples
            GROUP BY category, safety
            ORDER BY count DESC
            """
        ).fetchall()
        for row in rows:
            print(f"- {row['category']:18} {row['safety']:14} {row['count']}")

        latest = conn.execute("SELECT MAX(time) AS latest FROM process_samples").fetchone()["latest"]
        recent_cutoff = int(latest) - 3600
        print("\nTop recent waste candidates:")
        rows = conn.execute(
            """
            SELECT
                name,
                category,
                safety,
                recommendation,
                reason,
                ROUND(AVG(waste_score), 1) AS avg_waste,
                ROUND(MAX(expected_gain_mb), 1) AS max_gain,
                ROUND(MAX(private_mb), 1) AS max_private_mb,
                COUNT(*) AS samples
            FROM process_samples
            WHERE time >= ?
            GROUP BY name, category, safety, recommendation, reason
            ORDER BY avg_waste DESC, max_gain DESC
            LIMIT ?
            """,
            (recent_cutoff, max(1, args.limit)),
        ).fetchall()
        for row in rows:
            print(
                f"- {row['name']}: waste={row['avg_waste']} "
                f"gain={row['max_gain']}MB private={row['max_private_mb']}MB "
                f"type={row['category']} safety={row['safety']} action={row['recommendation']} "
                f"why={row['reason']}"
            )
    finally:
        conn.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
