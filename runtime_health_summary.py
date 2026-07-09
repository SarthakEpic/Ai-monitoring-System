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
    parser = argparse.ArgumentParser(description="Summarize Stage 7 runtime health samples.")
    parser.add_argument("--db", default="build/Debug/monitor.db", help="Path to monitor.db")
    parser.add_argument("--limit", type=int, default=12, help="Recent health rows to print")
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app for at least 60 seconds, then try again.")
        return 0

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    if not table_exists(conn, "runtime_health_samples"):
        print("runtime_health_samples table not found.")
        print("Rebuild and run the app once so migration version 11 can create it.")
        return 0

    total = conn.execute("SELECT COUNT(*) AS n FROM runtime_health_samples").fetchone()["n"]
    print(f"Runtime health rows: {total}")
    if total == 0:
        print("No runtime health samples recorded yet. Keep the app running a little longer.")
        return 0

    latest = conn.execute(
        """
        SELECT *
        FROM runtime_health_samples
        ORDER BY time DESC
        LIMIT 1
        """
    ).fetchone()

    print("\nLatest health:")
    print(f"  status:          {latest['status']}")
    print(f"  summary:         {latest['summary']}")
    print(f"  availability:    {latest['availability_score']:.1f}%")
    print(f"  model success:   {latest['model_success_rate']:.1f}%")
    print(f"  fallback rate:   {latest['fallback_rate']:.1f}%")
    print(f"  SQLite success:  {latest['storage_success_rate']:.1f}%")
    print(f"  avg latency:     {latest['avg_prediction_latency_ms']:.0f} ms")
    print(f"  last path:       {latest['prediction_path']}")
    print(f"  last failure:    {latest['last_failure']}")

    print("\nBy status:")
    for row in conn.execute(
        """
        SELECT status, COUNT(*) AS n,
               ROUND(AVG(availability_score), 1) AS avg_availability,
               ROUND(AVG(model_success_rate), 1) AS avg_model_success,
               ROUND(AVG(storage_success_rate), 1) AS avg_storage_success
        FROM runtime_health_samples
        GROUP BY status
        ORDER BY n DESC
        """
    ):
        print(
            f"  {row['status']:<10} rows={row['n']:<5} "
            f"availability={row['avg_availability']}% "
            f"model={row['avg_model_success']}% "
            f"sqlite={row['avg_storage_success']}%"
        )

    print(f"\nRecent {max(1, args.limit)} samples:")
    for row in conn.execute(
        """
        SELECT time, status, prediction_source, prediction_path,
               CAST(availability_score AS INT) AS availability,
               CAST(model_success_rate AS INT) AS model_ok,
               CAST(storage_success_rate AS INT) AS sqlite_ok,
               CAST(fallback_rate AS INT) AS fallback,
               CAST(avg_prediction_latency_ms AS INT) AS latency,
               model_attempts, model_failures, storage_failures, alerts
        FROM runtime_health_samples
        ORDER BY time DESC
        LIMIT ?
        """,
        (max(1, args.limit),),
    ):
        print(
            f"  t={row['time']} {row['status']:<10} src={row['prediction_source']:<10} "
            f"path={row['prediction_path']:<8} avail={row['availability']:>3}% "
            f"model={row['model_ok']:>3}% db={row['sqlite_ok']:>3}% "
            f"fallback={row['fallback']:>3}% latency={row['latency']:>4}ms "
            f"attempts={row['model_attempts']} mfails={row['model_failures']} "
            f"dbfails={row['storage_failures']} alerts={row['alerts']}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
