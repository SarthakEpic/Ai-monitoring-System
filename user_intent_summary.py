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
    parser = argparse.ArgumentParser(description="Inspect collected User Intent samples.")
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
        if not _table_exists(conn, "user_intent_samples"):
            print("user_intent_samples table not found. Run the rebuilt app first.")
            return 0

        total = conn.execute("SELECT COUNT(*) AS count FROM user_intent_samples").fetchone()["count"]
        print(f"User intent samples: {total}")
        if total == 0:
            print("No user intent samples yet. Run the app for at least 30-60 seconds.")
            return 0

        latest = conn.execute(
            """
            SELECT *
            FROM user_intent_samples
            ORDER BY time DESC
            LIMIT 1
            """
        ).fetchone()
        print("\nLatest intent:")
        print(f"- State: {latest['user_state']}")
        print(f"- App kind: {latest['app_kind']}")
        print(f"- Foreground: {latest['foreground_process']}")
        print(f"- Idle: {round(float(latest['idle_seconds']), 1)}s")
        print(f"- Focus duration: {round(float(latest['focus_duration_seconds']), 1)}s")
        print(f"- Fullscreen: {'yes' if latest['is_fullscreen'] else 'no'}")
        print(f"- Reason: {latest['reason']}")

        print("\nBy state and app kind:")
        rows = conn.execute(
            """
            SELECT user_state, app_kind, COUNT(*) AS count
            FROM user_intent_samples
            GROUP BY user_state, app_kind
            ORDER BY count DESC
            """
        ).fetchall()
        for row in rows:
            print(f"- {row['user_state']:8} {row['app_kind']:15} {row['count']}")

        print("\nRecent foreground apps:")
        rows = conn.execute(
            """
            SELECT
                foreground_process,
                app_kind,
                MAX(focus_duration_seconds) AS max_focus,
                COUNT(*) AS samples
            FROM user_intent_samples
            GROUP BY foreground_process, app_kind
            ORDER BY MAX(time) DESC
            LIMIT ?
            """,
            (max(1, args.limit),),
        ).fetchall()
        for row in rows:
            print(
                f"- {row['foreground_process']}: kind={row['app_kind']} "
                f"max_focus={round(float(row['max_focus']), 1)}s samples={row['samples']}"
            )
    finally:
        conn.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
