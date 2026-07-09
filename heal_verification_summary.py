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
    parser = argparse.ArgumentParser(description="Summarize Stage 5 healing verification simulations.")
    parser.add_argument("--db", default="build/Debug/monitor.db", help="Path to monitor.db")
    parser.add_argument("--limit", type=int, default=10, help="Recent verifications to print")
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app for at least 30-60 seconds, then try again.")
        return 0

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    if not table_exists(conn, "heal_verifications"):
        print("heal_verifications table not found.")
        print("Rebuild and run the app once so migration version 9 can create it.")
        return 0

    total = conn.execute("SELECT COUNT(*) AS n FROM heal_verifications").fetchone()["n"]
    print(f"Heal verification rows: {total}")
    if total == 0:
        print("No verifications recorded yet. Keep the app running a little longer.")
        return 0

    print("\nBy status:")
    for row in conn.execute(
        """
        SELECT status, outcome_label, COUNT(*) AS n,
               ROUND(AVG(risk_delta_estimate), 1) AS avg_delta,
               ROUND(AVG(confidence), 1) AS avg_conf
        FROM heal_verifications
        GROUP BY status, outcome_label
        ORDER BY n DESC
        """
    ):
        print(
            f"  {row['status']:<18} outcome={row['outcome_label']:<18} "
            f"rows={row['n']:<5} avg_delta={row['avg_delta']} avg_conf={row['avg_conf']}"
        )

    print("\nTop simulated actions:")
    for row in conn.execute(
        """
        SELECT action_name, target_name, COUNT(*) AS n,
               ROUND(AVG(risk_before), 1) AS risk_before,
               ROUND(AVG(risk_after_estimate), 1) AS risk_after
        FROM heal_verifications
        GROUP BY action_name, target_name
        ORDER BY n DESC
        LIMIT 8
        """
    ):
        print(
            f"  action={row['action_name']:<28} target={row['target_name']:<18} "
            f"rows={row['n']} risk={row['risk_before']}->{row['risk_after']}"
        )

    print(f"\nRecent {max(1, args.limit)} verifications:")
    for row in conn.execute(
        """
        SELECT time, status, outcome_label, action_name, target_name,
               CAST(risk_before AS INT) AS risk_before,
               CAST(risk_after_estimate AS INT) AS risk_after,
               CAST(risk_delta_estimate AS INT) AS risk_delta,
               CAST(confidence AS INT) AS confidence,
               reason
        FROM heal_verifications
        ORDER BY time DESC
        LIMIT ?
        """,
        (max(1, args.limit),),
    ):
        print(
            f"  t={row['time']} {row['status']:<18} outcome={row['outcome_label']:<18} "
            f"action={row['action_name']:<28} target={row['target_name']:<18} "
            f"risk={row['risk_before']}->{row['risk_after']} delta={row['risk_delta']}% "
            f"conf={row['confidence']}%"
        )
        print(f"      {row['reason']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
