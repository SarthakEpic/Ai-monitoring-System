import argparse
import sqlite3
from pathlib import Path


def table_exists(conn: sqlite3.Connection, name: str) -> bool:
    row = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
        (name,),
    ).fetchone()
    return row is not None


def print_latest(conn: sqlite3.Connection, table: str, fields: list[str]) -> None:
    if not table_exists(conn, table):
        print(f"{table}: missing")
        return

    row = conn.execute(
        f"SELECT {', '.join(fields)} FROM {table} ORDER BY time DESC LIMIT 1"
    ).fetchone()
    if not row:
        print(f"{table}: no samples yet")
        return

    print(f"\n{table}")
    for key, value in zip(fields, row):
        print(f"  {key}: {value}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize low-end autopilot, agent, and benchmark proof telemetry.")
    parser.add_argument("--db", default="build\\Debug\\monitor.db", help="Path to monitor.db")
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app for at least 60 seconds, then try again.")
        return 0

    with sqlite3.connect(db_path) as conn:
        print_latest(
            conn,
            "low_end_autopilot_samples",
            [
                "time",
                "status",
                "mode",
                "summary",
                "actions_recommended",
                "user_apps_touched",
                "estimated_recovered_ram_mb",
                "estimated_cpu_drop_percent",
                "primary_action",
                "primary_target",
            ],
        )
        print_latest(
            conn,
            "background_agent_samples",
            [
                "time",
                "status",
                "mode",
                "summary",
                "tray_icon_ready",
                "silent_monitoring",
                "start_on_boot_configured",
                "quick_restore_available",
                "quick_restore_status",
            ],
        )
        print_latest(
            conn,
            "benchmark_proof_samples",
            [
                "time",
                "status",
                "summary",
                "before_cpu",
                "after_cpu_estimate",
                "before_memory",
                "after_memory_estimate",
                "recovered_ram_mb",
                "actions_recommended",
                "user_apps_touched",
                "confidence",
            ],
        )

        if table_exists(conn, "autopilot_actions"):
            rows = conn.execute(
                """
                SELECT rank, action_name, target_name, expected_ram_mb, expected_cpu_drop_percent, reason
                FROM autopilot_actions
                WHERE time = (SELECT MAX(time) FROM autopilot_actions)
                ORDER BY rank
                LIMIT 8
                """
            ).fetchall()
            if rows:
                print("\nlatest autopilot actions")
                for rank, action, target, ram, cpu, reason in rows:
                    print(f"  {rank}. {action} -> {target} | RAM {ram:.0f} MB | CPU {cpu:.0f}% | {reason}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
