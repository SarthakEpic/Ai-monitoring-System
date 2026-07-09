import argparse
import sqlite3
from pathlib import Path


def table_exists(conn: sqlite3.Connection, name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (name,),
    ).fetchone()
    return row is not None


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize Stage 4 auto-heal dry-run plans.")
    parser.add_argument("--db", default="build/Debug/monitor.db", help="Path to monitor.db")
    parser.add_argument("--limit", type=int, default=10, help="Recent plans to print")
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app for at least 30-60 seconds, then try again.")
        return 0

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    if not table_exists(conn, "heal_plans"):
        print("heal_plans table not found.")
        print("Rebuild and run the app once so migration version 8 can create it.")
        return 0

    total = conn.execute("SELECT COUNT(*) AS n FROM heal_plans").fetchone()["n"]
    print(f"Heal plan rows: {total}")
    if total == 0:
        print("No plans recorded yet. Keep the app running a little longer.")
        return 0

    print("\nBy status:")
    for row in conn.execute(
        """
        SELECT status, COUNT(*) AS n, ROUND(AVG(readiness_score), 1) AS avg_readiness
        FROM heal_plans
        GROUP BY status
        ORDER BY n DESC
        """
    ):
        print(f"  {row['status']:<16} rows={row['n']:<5} avg_ready={row['avg_readiness']}")

    print("\nTop actions:")
    for row in conn.execute(
        """
        SELECT action_type, action_name, gate, COUNT(*) AS n
        FROM heal_plans
        GROUP BY action_type, action_name, gate
        ORDER BY n DESC
        LIMIT 8
        """
    ):
        print(
            f"  {row['action_type']:<18} action={row['action_name']:<28} "
            f"gate={row['gate']:<22} rows={row['n']}"
        )

    print(f"\nRecent {max(1, args.limit)} plans:")
    for row in conn.execute(
        """
        SELECT time, status, action_name, target_name, gate,
               CAST(readiness_score AS INT) AS ready,
               CAST(expected_gain_mb AS INT) AS gain,
               execution_mode, blocked_reason, summary
        FROM heal_plans
        ORDER BY time DESC
        LIMIT ?
        """,
        (max(1, args.limit),),
    ):
        print(
            f"  t={row['time']} {row['status']:<16} ready={row['ready']:>3}% "
            f"action={row['action_name']:<28} target={row['target_name']:<18} "
            f"gate={row['gate']:<18} gain={row['gain']}MB mode={row['execution_mode']} "
            f"reason={row['blocked_reason']}"
        )
        print(f"      {row['summary']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
