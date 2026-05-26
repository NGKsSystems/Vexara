"""Inspect Vexara pipeline queue.db for recent tasks, events, and deferrals."""
from __future__ import annotations

import json
import sqlite3
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: diagnose_queue.py <queue.db> [limit]")
        return 1

    db_path = Path(sys.argv[1])
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 20

    if not db_path.is_file():
        print(f"No pipeline queue database at: {db_path}")
        return 0

    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    print("--- Recent tasks ---")
    rows = cur.execute(
        "SELECT id, status, priority, retry_count, max_retries, deferred_until, "
        "payload_json, updated_at FROM tasks ORDER BY id DESC LIMIT ?",
        (limit,),
    ).fetchall()
    if not rows:
        print("(none)")
    for row in rows:
        print(
            f"task {row['id']}  status={row['status']}  "
            f"retries={row['retry_count']}/{row['max_retries']}  updated={row['updated_at']}"
        )
        try:
            payload = json.loads(row["payload_json"])
        except json.JSONDecodeError:
            payload = {}
        stage = payload.get("stage", payload.get("kind", "?"))
        print(f"  stage={stage}  pipeline_id={payload.get('pipeline_id', '?')}")
        if payload.get("plan_task_id") is not None:
            print(f"  plan_task_id={payload.get('plan_task_id')}")
        if payload.get("chosen_backend"):
            print(f"  chosen_backend={payload.get('chosen_backend')}")

    print("")
    print("--- Recent events ---")
    events = cur.execute(
        "SELECT task_id, agent_name, event_type, timestamp, payload_json "
        "FROM events ORDER BY id DESC LIMIT ?",
        (limit,),
    ).fetchall()
    if not events:
        print("(none)")
    for row in events:
        print(
            f"task {row['task_id']}  {row['timestamp']}  "
            f"{row['agent_name']}/{row['event_type']}"
        )
        preview = (row["payload_json"] or "")[:220]
        if preview:
            print(f"  {preview}")

    print("")
    print("--- Deferred tasks ---")
    deferred = cur.execute(
        "SELECT task_id, deferred_until, reason FROM deferred_tasks ORDER BY id DESC LIMIT ?",
        (limit,),
    ).fetchall()
    if not deferred:
        print("(none)")
    else:
        for row in deferred:
            print(f"task {row['task_id']}  until={row['deferred_until']}  reason={row['reason']}")

    conn.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
