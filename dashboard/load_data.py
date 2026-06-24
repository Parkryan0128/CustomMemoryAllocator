"""Load benchmark CSV and lifecycle trace JSON for the unified dashboard."""

from __future__ import annotations

import csv
import json
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

LIFECYCLE_ORDER = ["interleaved", "batch"]

LIFECYCLE_LABELS = {
    "interleaved": "Interleaved",
    "batch": "Batch",
}


def _csv_candidates() -> list[Path]:
    return [DATA_DIR / "results.csv", ROOT / "results.csv"]


def _trace_path(workload: str) -> list[Path]:
    return [
        DATA_DIR / f"lifecycle_trace_{workload}.json",
        ROOT / f"lifecycle_trace_{workload}.json",
        ROOT / "lifecycle_trace.json",
    ]


def load_benchmark_rows() -> list[dict]:
    csv_path: Path | None = None
    for candidate in _csv_candidates():
        if candidate.exists():
            csv_path = candidate
            break

    if csv_path is None:
        print(
            "Error: results.csv not found. Run ./allocator_test plot or make dashboard.",
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


def _load_trace_file(path: Path) -> dict:
    with path.open(encoding="utf-8") as file:
        data = json.load(file)
    if "meta" not in data or "samples" not in data or not data["samples"]:
        raise ValueError(f"{path} is missing meta, samples, or has empty samples")
    return data


def load_lifecycle_traces() -> dict[str, dict]:
    traces: dict[str, dict] = {}

    for workload in LIFECYCLE_ORDER:
        for path in _trace_path(workload):
            if not path.exists():
                continue
            try:
                data = _load_trace_file(path)
            except ValueError:
                continue
            if path.name == "lifecycle_trace.json":
                meta_workload = data["meta"].get("workload", workload)
                if meta_workload in LIFECYCLE_ORDER:
                    traces[meta_workload] = data
                break
            traces[workload] = data
            break

    return traces
