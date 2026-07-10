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
    parser = argparse.ArgumentParser(description="Summarize Stage 8 adaptive device baseline samples.")
    parser.add_argument("--db", default="build/Debug/monitor.db", help="Path to monitor.db")
    parser.add_argument("--limit", type=int, default=12, help="Recent baseline rows to print")
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Run the rebuilt app for at least 60 seconds, then try again.")
        return 0

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    if not table_exists(conn, "adaptive_baseline_samples"):
        print("adaptive_baseline_samples table not found.")
        print("Rebuild and run the app once so migration version 12 can create it.")
        return 0

    total = conn.execute("SELECT COUNT(*) AS n FROM adaptive_baseline_samples").fetchone()["n"]
    print(f"Adaptive baseline rows: {total}")
    if total == 0:
        print("No baseline samples recorded yet. Keep the app running a little longer.")
        return 0

    latest = conn.execute(
        """
        SELECT *
        FROM adaptive_baseline_samples
        ORDER BY time DESC
        LIMIT 1
        """
    ).fetchone()

    print("\nLatest baseline:")
    print(f"  status:        {latest['status']}")
    print(f"  summary:       {latest['summary']}")
    print(f"  ready:         {bool(latest['ready'])}")
    print(f"  samples:       {latest['sample_count']}")
    print(f"  confidence:    {latest['confidence']:.1f}%")
    print(f"  anomaly:       {latest['anomaly_score']:.1f}%")
    print(f"  risk adjust:   {latest['risk_adjustment']:.1f}")
    print(f"  dominant:      {latest['dominant_metric']}")

    print("\nLearned means:")
    print(
        f"  CPU {latest['cpu_mean']:.1f}% | MEM {latest['memory_mean']:.1f}% | "
        f"DISK FREE {latest['disk_free_mean']:.1f}%"
    )
    print(
        f"  NET {latest['network_mean']:.1f} KB/s | PROC {latest['process_count_mean']:.1f} | "
        f"TOP CPU {latest['top_cpu_mean']:.1f}% | TOP MEM {latest['top_memory_mean']:.1f} MB"
    )

    print("\nBy status:")
    for row in conn.execute(
        """
        SELECT status, dominant_metric, COUNT(*) AS n,
               ROUND(AVG(anomaly_score), 1) AS avg_anomaly,
               ROUND(AVG(confidence), 1) AS avg_confidence
        FROM adaptive_baseline_samples
        GROUP BY status, dominant_metric
        ORDER BY n DESC
        """
    ):
        print(
            f"  {row['status']:<12} metric={row['dominant_metric']:<20} "
            f"rows={row['n']:<5} anomaly={row['avg_anomaly']}% confidence={row['avg_confidence']}%"
        )

    print(f"\nRecent {max(1, args.limit)} samples:")
    for row in conn.execute(
        """
        SELECT time, status, dominant_metric,
               CAST(sample_count AS INT) AS samples,
               CAST(confidence AS INT) AS confidence,
               CAST(anomaly_score AS INT) AS anomaly,
               ROUND(risk_adjustment, 1) AS risk_adjustment,
               CAST(cpu_deviation AS INT) AS cpu_dev,
               CAST(memory_deviation AS INT) AS mem_dev,
               CAST(disk_deviation AS INT) AS disk_dev,
               CAST(network_deviation AS INT) AS net_dev,
               CAST(process_deviation AS INT) AS proc_dev
        FROM adaptive_baseline_samples
        ORDER BY time DESC
        LIMIT ?
        """,
        (max(1, args.limit),),
    ):
        print(
            f"  t={row['time']} {row['status']:<12} metric={row['dominant_metric']:<18} "
            f"samples={row['samples']:<4} conf={row['confidence']:>3}% "
            f"anom={row['anomaly']:>3}% adj={row['risk_adjustment']:>4} "
            f"dev(cpu/mem/disk/net/proc)="
            f"{row['cpu_dev']}/{row['mem_dev']}/{row['disk_dev']}/{row['net_dev']}/{row['proc_dev']}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
