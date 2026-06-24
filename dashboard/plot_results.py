#!/usr/bin/env python3
"""Generate benchmark_*.png charts from results.csv (legacy matplotlib output)."""

from __future__ import annotations

import sys
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

from load_data import DATA_DIR, ROOT, _csv_candidates


def resolve_csv_path() -> Path:
    for candidate in _csv_candidates():
        if candidate.exists():
            return candidate
    print(
        "Error: results.csv not found. Run ./allocator_test plot or make plot.",
        file=sys.stderr,
    )
    sys.exit(1)


def main() -> None:
    csv_path = resolve_csv_path()
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    df = pd.read_csv(csv_path)
    benchmark_types = df["benchmark_type"].unique()

    for bench_type in benchmark_types:
        print(f"Generating plot for: {bench_type}...")

        subset = df[df["benchmark_type"] == bench_type]

        plt.figure(figsize=(10, 6))
        sns.lineplot(
            data=subset,
            x="num_allocations",
            y="time_ms",
            hue="allocator_type",
            marker="o",
        ).set_title(f"Performance: {bench_type.replace('_', ' ').title()}")

        plt.xlabel("Number of Allocations")
        plt.ylabel("Total Time (ms)")
        plt.grid(True)
        plt.legend(title="Allocator")

        out_path = DATA_DIR / f"benchmark_{bench_type}.png"
        plt.savefig(out_path)
        print(f"Saved plot to {out_path.relative_to(ROOT)}")

    print("\nAll plots generated.")


if __name__ == "__main__":
    main()
