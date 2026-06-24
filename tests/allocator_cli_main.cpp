#include "lifecycle_trace.hpp"

#include <iostream>
#include <string>

// Benchmark/plot entry point (defined in benchmark_main.cpp).
int run_benchmark_cli(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) == "trace") {
        return run_lifecycle_trace(argc, argv);
    }
    return run_benchmark_cli(argc, argv);
}
