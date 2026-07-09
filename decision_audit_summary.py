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
    parser = argparse.ArgumentParser(description="Summarize Stage 3 decision audit records.")
    parser.add_argument("--db", default="build/Debug/monitor.db", help="Path to monitor.db")
    parser.add_argument("--limit", type=int, default=10, help="Recent rows to print")
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app for at least 30-60 seconds, then try again.")
        return 0

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    if not table_exists(conn, "decision_audits"):
        print("decision_audits table not found.")
        print("Rebuild and run the app once so migration version 7 can create it.")
        return 0

    total = conn.execute("SELECT COUNT(*) AS n FROM decision_audits").fetchone()["n"]
    print(f"Decision audit rows: {total}")
    if total == 0:
        print("No decisions recorded yet. Keep the app running a little longer.")
        return 0

    print("\nBy level:")
    for row in conn.execute(
        """
        SELECT level, COUNT(*) AS n, ROUND(AVG(risk_score), 1) AS avg_risk
        FROM decision_audits
        GROUP BY level
        ORDER BY n DESC
        """
    ):
        print(f"  {row['level']:<8} rows={row['n']:<5} avg_risk={row['avg_risk']}")

    print("\nTop root causes:")
    for row in conn.execute(
        """
        SELECT root_cause, safety_gate, COUNT(*) AS n
        FROM decision_audits
        GROUP BY root_cause, safety_gate
        ORDER BY n DESC
        LIMIT 8
        """
    ):
        print(f"  {row['root_cause']:<14} gate={row['safety_gate']:<22} rows={row['n']}")

    print(f"\nRecent {max(1, args.limit)} decisions:")
    for row in conn.execute(
        """
        SELECT time, level, CAST(risk_score AS INT) AS risk, root_cause,
               recommended_action, action_target_name, safety_gate,
               blocked_reason, CAST(expected_gain_mb AS INT) AS gain
        FROM decision_audits
        ORDER BY time DESC
        LIMIT ?
        """,
        (max(1, args.limit),),
    ):
        print(
            f"  t={row['time']} {row['level']:<8} risk={row['risk']:>3}% "
            f"cause={row['root_cause']:<13} action={row['recommended_action']:<24} "
            f"target={row['action_target_name']:<18} gate={row['safety_gate']:<18} "
            f"gain={row['gain']}MB reason={row['blocked_reason']}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
