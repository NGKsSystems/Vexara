"""Reset pipeline tasks stuck in 'running' back to 'pending'."""
import sqlite3
import sys
from pathlib import Path

def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: reset_stuck_queue.py <queue.db>")
        return 1
    db_path = Path(sys.argv[1])
    if not db_path.is_file():
        print(f"Not found: {db_path}")
        return 1
    conn = sqlite3.connect(db_path)
    cur = conn.execute("UPDATE tasks SET status='pending' WHERE status='running'")
    conn.commit()
    print(f"Reset {cur.rowcount} stuck running task(s) to pending")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
