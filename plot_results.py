import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

try:
    df = pd.read_csv("results.csv")
except FileNotFoundError:
    print("Error: results.csv not found. Run the C++ benchmark first.")
    exit()

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

    plt.savefig(f"benchmark_{bench_type}.png")
    print(f"Saved plot to benchmark_{bench_type}.png")

print("\nAll plots generated.")
