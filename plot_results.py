import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

# Load the benchmark results from the CSV file.
try:
    df = pd.read_csv("results.csv")
except FileNotFoundError:
    print("Error: results.csv not found. Run the C++ benchmark first.")
    exit()

# Get a list of the unique benchmark types you ran.
benchmark_types = df["benchmark_type"].unique()

# Create a separate plot for each benchmark type.
for bench_type in benchmark_types:
    print(f"Generating plot for: {bench_type}...")
    
    # Filter the data for the current benchmark type.
    subset = df[df["benchmark_type"] == bench_type]
    
    # Create a new figure for the plot.
    plt.figure(figsize=(10, 6))
    
    # Use seaborn to create a line plot.
    # It will automatically create separate, colored lines for 'system' and 'custom'.
    sns.lineplot(
        data=subset,
        x="num_allocations",
        y="time_ms",
        hue="allocator_type",
        marker="o" # Add markers to data points
    ).set_title(f"Performance: {bench_type.replace('_', ' ').title()}")

    plt.xlabel("Number of Allocations")
    plt.ylabel("Total Time (ms)")
    plt.grid(True)
    plt.legend(title="Allocator")
    
    # Save the plot to a file.
    plt.savefig(f"benchmark_{bench_type}.png")
    print(f"Saved plot to benchmark_{bench_type}.png")



try:
    df = pd.read_csv("results2.csv")
except FileNotFoundError:
    print("Error: results.csv not found. Run the C++ benchmark first.")
    exit()

# Get a list of the unique benchmark types you ran.
benchmark_types = df["benchmark_type"].unique()

# Create a separate plot for each benchmark type.
for bench_type in benchmark_types:
    print(f"Generating plot for: {bench_type}...")
    
    # Filter the data for the current benchmark type.
    subset = df[df["benchmark_type"] == bench_type]
    
    # Create a new figure for the plot.
    plt.figure(figsize=(10, 6))
    
    # Use seaborn to create a line plot.
    # It will automatically create separate, colored lines for 'system' and 'custom'.
    sns.lineplot(
        data=subset,
        x="num_allocations",
        y="time_ms",
        hue="allocator_type",
        marker="o" # Add markers to data points
    ).set_title(f"Performance: {bench_type.replace('_', ' ').title()}")

    plt.xlabel("Number of Allocations")
    plt.ylabel("Total Time (ms)")
    plt.grid(True)
    plt.legend(title="Allocator")
    
    # Save the plot to a file.
    plt.savefig(f"benchmark_{bench_type}.png")
    print(f"Saved plot to benchmark_{bench_type}.png")



try:
    df = pd.read_csv("results3.csv")
except FileNotFoundError:
    print("Error: results.csv not found. Run the C++ benchmark first.")
    exit()

# Get a list of the unique benchmark types you ran.
benchmark_types = df["benchmark_type"].unique()

# Create a separate plot for each benchmark type.
for bench_type in benchmark_types:
    print(f"Generating plot for: {bench_type}...")
    
    # Filter the data for the current benchmark type.
    subset = df[df["benchmark_type"] == bench_type]
    
    # Create a new figure for the plot.
    plt.figure(figsize=(10, 6))
    
    # Use seaborn to create a line plot.
    # It will automatically create separate, colored lines for 'system' and 'custom'.
    sns.lineplot(
        data=subset,
        x="num_allocations",
        y="throughput_M_ops_per_sec",
        hue="allocator_type",
        marker="o" # Add markers to data points
    ).set_title(f"Performance: {bench_type.replace('_', ' ').title()}")

    plt.xlabel("Number of Allocations")
    plt.ylabel("throughput_M_ops_per_sec")
    plt.grid(True)
    plt.legend(title="Allocator")
    
    # Save the plot to a file.
    plt.savefig(f"benchmark_{bench_type}.png")
    print(f"Saved plot to benchmark_{bench_type}.png")


print("\nAll plots generated.")