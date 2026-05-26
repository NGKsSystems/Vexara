"""Cancel all pending root planning tasks in the pipeline queue."""
import sqlite3
import sys
from pathlib import Path

def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: cancel_pending_queue.py <queue.db>")
        return 1
    db_path = Path(sys.argv[1])
    if not db_path.is_file():
        print(f"Not found: {db_path}")
        return 1
    conn = sqlite3.connect(db_path)
    rows = conn.execute("SELECT id, payload_json FROM tasks WHERE status='pending'").fetchall()
    cancelled = 0
    for task_id, payload in rows:
        if "planning" in payload and "hierarchical_stage" in payload:
            cur = conn.execute(
                "UPDATE tasks SET status='completed' WHERE id=? AND status='pending'",
                (task_id,),
            )
            cancelled += cur.rowcount
    conn.commit()
    print(f"Cancelled {cancelled} pending planning task(s)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
