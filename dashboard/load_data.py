"""Load benchmark CSV for the dashboard."""

from __future__ import annotations

import csv
import sys
from pathlib import Path

DASHBOARD_DIR = Path(__file__).resolve().parent
ROOT = DASHBOARD_DIR.parent
DATA_DIR = DASHBOARD_DIR / "data"

BENCHMARK_ORDER = [
    "single_interleaved",
    "single_batch",
    "single_random_mix",
    "multi_interleaved",
    "multi_batch",
    "multi_random_mix",
]

BENCHMARK_LABELS = {
    "single_interleaved": "Single-thread · Interleaved",
    "single_batch": "Single-thread · Batch",
    "single_random_mix": "Single-thread · Random mix",
    "multi_interleaved": "Multi-thread · Interleaved",
    "multi_batch": "Multi-thread · Batch",
    "multi_random_mix": "Multi-thread · Random mix",
}


def load_benchmark_rows() -> list[dict]:
    csv_path = DATA_DIR / "results.csv"
    if not csv_path.exists():
        print(
            f"Error: {csv_path} not found. Run make dashboard or ./allocator_test plot.",
            file=sys.stderr,
        )
        sys.exit(1)

    rows: list[dict] = []
    with csv_path.open(newline="") as file:
        for row in csv.DictReader(file):
            rows.append(
                {
                    "allocator_type": row["allocator_type"],
                    "benchmark_type": row["benchmark_type"],
                    "num_allocations": int(row["num_allocations"]),
                    "time_ms": int(row["time_ms"]),
                }
            )

    if not rows:
        print(f"Error: {csv_path} is empty.", file=sys.stderr)
        sys.exit(1)

    return rows
