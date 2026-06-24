#pragma once

// Runs: allocator_test trace --workload <name> --ops N --sample M [--out path.json]
// Separate from the performance benchmark; samples allocator.stats() periodically.
int run_lifecycle_trace(int argc, char* argv[]);
