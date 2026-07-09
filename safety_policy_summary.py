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
    parser = argparse.ArgumentParser(description="Summarize Stage 6 safety policy evaluations.")
    parser.add_argument("--db", default="build/Debug/monitor.db", help="Path to monitor.db")
    parser.add_argument("--limit", type=int, default=10, help="Recent policy rows to print")
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app for at least 30-60 seconds, then try again.")
        return 0

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    if not table_exists(conn, "safety_policy_evaluations"):
        print("safety_policy_evaluations table not found.")
        print("Rebuild and run the app once so migration version 10 can create it.")
        return 0

    total = conn.execute("SELECT COUNT(*) AS n FROM safety_policy_evaluations").fetchone()["n"]
    print(f"Safety policy rows: {total}")
    if total == 0:
        print("No policy evaluations recorded yet. Keep the app running a little longer.")
        return 0

    print("\nBy policy level:")
    for row in conn.execute(
        """
        SELECT level, reason_code, COUNT(*) AS n,
               ROUND(AVG(policy_score), 1) AS avg_score
        FROM safety_policy_evaluations
        GROUP BY level, reason_code
        ORDER BY n DESC
        """
    ):
        print(
            f"  {row['level']:<20} code={row['reason_code']:<24} "
            f"rows={row['n']:<5} avg_score={row['avg_score']}"
        )

    print("\nBlocked/protected targets:")
    for row in conn.execute(
        """
        SELECT target_name, reason_code, COUNT(*) AS n
        FROM safety_policy_evaluations
        WHERE hard_block = 1 OR target_protected = 1 OR target_denied = 1
        GROUP BY target_name, reason_code
        ORDER BY n DESC
        LIMIT 8
        """
    ):
        print(f"  target={row['target_name']:<22} code={row['reason_code']:<24} rows={row['n']}")

    print(f"\nRecent {max(1, args.limit)} evaluations:")
    for row in conn.execute(
        """
        SELECT time, level, reason_code, target_name,
               CAST(policy_score AS INT) AS score,
               execution_eligible, hard_block, requires_approval,
               simulation_only, reason
        FROM safety_policy_evaluations
        ORDER BY time DESC
        LIMIT ?
        """,
        (max(1, args.limit),),
    ):
        print(
            f"  t={row['time']} {row['level']:<20} score={row['score']:>3}% "
            f"code={row['reason_code']:<24} target={row['target_name']:<18} "
            f"eligible={row['execution_eligible']} hard={row['hard_block']} "
            f"review={row['requires_approval']} sim={row['simulation_only']}"
        )
        print(f"      {row['reason']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
