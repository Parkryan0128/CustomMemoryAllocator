#include "lifecycle_trace.hpp"

#include "FixedBlockAllocator.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr size_t kBlockSize = 32;

enum class Workload {
    Interleaved,
    Batch,
};

struct TraceSample {
    size_t op = 0;
    const char* event = "step";
    double elapsed_ms = 0.0;
    size_t live_blocks = 0;
    size_t live_bytes = 0;
    size_t active_pages = 0;
    size_t mapped_bytes = 0;
    size_t free_blocks = 0;
    size_t capacity_blocks = 0;
};

struct TraceConfig {
    Workload workload = Workload::Interleaved;
    size_t ops = 100000;
    size_t sample_interval = 2000;
    std::string output_path = "lifecycle_trace.json";
};

using Clock = std::chrono::high_resolution_clock;

void print_trace_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name
              << " trace --workload <interleaved|batch> --ops N --sample M [--out "
                 "path.json]\n\n"
              << "Samples FixedBlockAllocator stats during a workload run and writes JSON for the "
                 "lifecycle dashboard.\n\n"
              << "Examples:\n"
              << "  " << prog_name
              << " trace --workload interleaved --ops 100000 --sample 2000\n"
              << "  " << prog_name
              << " trace --workload batch --ops 50000 --sample 1000 --out batch_trace.json\n";
}

bool parse_workload(const std::string& name, Workload& out) {
    if (name == "interleaved") {
        out = Workload::Interleaved;
        return true;
    }
    if (name == "batch") {
        out = Workload::Batch;
        return true;
    }
    return false;
}

const char* workload_name(Workload workload) {
    switch (workload) {
    case Workload::Interleaved:
        return "interleaved";
    case Workload::Batch:
        return "batch";
    }
    return "unknown";
}

bool parse_size_t_arg(const char* value, size_t& out) {
    if (value == nullptr || *value == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        return false;
    }
    out = static_cast<size_t>(parsed);
    return true;
}

bool parse_trace_config(int argc, char* argv[], TraceConfig& config) {
    if (argc < 2 || std::string(argv[1]) != "trace") {
        return false;
    }

    bool have_workload = false;
    bool have_ops = false;
    bool have_sample = false;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--workload" && i + 1 < argc) {
            if (!parse_workload(argv[++i], config.workload)) {
                std::cerr << "Error: unknown workload '" << argv[i] << "'\n";
                return false;
            }
            have_workload = true;
        } else if (arg == "--ops" && i + 1 < argc) {
            if (!parse_size_t_arg(argv[++i], config.ops) || config.ops == 0) {
                std::cerr << "Error: --ops must be a positive integer\n";
                return false;
            }
            have_ops = true;
        } else if (arg == "--sample" && i + 1 < argc) {
            if (!parse_size_t_arg(argv[++i], config.sample_interval) ||
                config.sample_interval == 0) {
                std::cerr << "Error: --sample must be a positive integer\n";
                return false;
            }
            have_sample = true;
        } else if (arg == "--out" && i + 1 < argc) {
            config.output_path = argv[++i];
        } else {
            std::cerr << "Error: unknown or incomplete argument '" << arg << "'\n";
            return false;
        }
    }

    if (!have_workload || !have_ops || !have_sample) {
        std::cerr << "Error: --workload, --ops, and --sample are required\n\n";
        return false;
    }
    return true;
}

TraceSample make_sample(size_t op,
                        const char* event,
                        double elapsed_ms,
                        const cma::FixedBlockAllocator<kBlockSize>::Stats& stats) {
    return TraceSample{op,
                       event,
                       elapsed_ms,
                       stats.live_blocks,
                       stats.live_bytes,
                       stats.active_pages,
                       stats.mapped_bytes,
                       stats.free_blocks,
                       stats.capacity_blocks};
}

class TraceRecorder {
public:
    explicit TraceRecorder(size_t sample_interval) : m_sample_interval(sample_interval) {}

    void record(size_t op,
                const char* event,
                double elapsed_ms,
                const cma::FixedBlockAllocator<kBlockSize>& allocator) {
        const cma::FixedBlockAllocator<kBlockSize>::Stats stats = allocator.stats();
        m_samples.push_back(make_sample(op, event, elapsed_ms, stats));
        m_last_live_blocks = stats.live_blocks;
    }

    void maybe_record(size_t op,
                      const char* event,
                      double elapsed_ms,
                      size_t last_active_pages,
                      const cma::FixedBlockAllocator<kBlockSize>& allocator) {
        const cma::FixedBlockAllocator<kBlockSize>::Stats stats = allocator.stats();
        const bool interval_hit = (op % m_sample_interval) == 0;
        const bool page_changed = stats.active_pages != last_active_pages;
        const bool live_changed =
            m_last_live_blocks != SIZE_MAX && stats.live_blocks != m_last_live_blocks;
        const bool final_op = m_total_ops > 0 && op + 1 >= m_total_ops;
        const bool sub_sample =
            live_changed && m_sample_interval > 1U && (op % (m_sample_interval / 4U)) == 0U;

        if (interval_hit || page_changed || sub_sample || final_op) {
            const char* sample_event = event;
            if (live_changed) {
                if (stats.live_blocks > m_last_live_blocks) {
                    sample_event = "alloc";
                } else if (stats.live_blocks < m_last_live_blocks) {
                    sample_event = "free";
                }
            }
            m_samples.push_back(make_sample(op, sample_event, elapsed_ms, stats));
        }
        m_last_live_blocks = stats.live_blocks;
    }

    void set_total_ops(size_t total_ops) { m_total_ops = total_ops; }

    size_t sample_interval() const { return m_sample_interval; }

    const std::vector<TraceSample>& samples() const { return m_samples; }

private:
    size_t m_sample_interval = 1;
    size_t m_total_ops = 0;
    size_t m_last_live_blocks = SIZE_MAX;
    std::vector<TraceSample> m_samples;
};

void record_step(size_t op,
                 const char* event,
                 TraceRecorder& recorder,
                 size_t& last_pages,
                 const Clock::time_point& start,
                 const cma::FixedBlockAllocator<kBlockSize>& allocator) {
    const auto now = Clock::now();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(now - start).count();
    const size_t pages = allocator.stats().active_pages;
    recorder.maybe_record(op, event, elapsed_ms, last_pages, allocator);
    last_pages = pages;
}

void record_now(size_t op,
                const char* event,
                TraceRecorder& recorder,
                size_t& last_pages,
                const Clock::time_point& start,
                const cma::FixedBlockAllocator<kBlockSize>& allocator) {
    const auto now = Clock::now();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(now - start).count();
    recorder.record(op, event, elapsed_ms, allocator);
    last_pages = allocator.stats().active_pages;
}

void run_interleaved_trace(cma::FixedBlockAllocator<kBlockSize>& allocator,
                           size_t ops,
                           TraceRecorder& recorder,
                           const Clock::time_point& start) {
    recorder.set_total_ops(ops);
    size_t last_pages = allocator.stats().active_pages;
    record_now(0, "start", recorder, last_pages, start, allocator);

    for (size_t i = 0; i < ops; ++i) {
        void* block = allocator.allocate();
        if (block != nullptr) {
            auto* bytes = static_cast<unsigned char*>(block);
            bytes[0] = static_cast<unsigned char>(i);

            if ((i % recorder.sample_interval()) == 0) {
                record_now(i, "alloc", recorder, last_pages, start, allocator);
            }

            allocator.deallocate(block);

            if ((i % recorder.sample_interval()) == 0) {
                record_now(i, "free", recorder, last_pages, start, allocator);
            }
        } else if ((i % recorder.sample_interval()) == 0) {
            record_now(i, "step", recorder, last_pages, start, allocator);
        }
    }
}

void run_batch_trace(cma::FixedBlockAllocator<kBlockSize>& allocator,
                     size_t ops,
                     TraceRecorder& recorder,
                     const Clock::time_point& start) {
    const size_t total_steps = ops * 2;
    recorder.set_total_ops(total_steps);
    size_t last_pages = allocator.stats().active_pages;
    record_now(0, "start", recorder, last_pages, start, allocator);

    std::vector<void*> blocks;
    blocks.reserve(ops);

    for (size_t i = 0; i < ops; ++i) {
        void* block = allocator.allocate();
        if (block != nullptr) {
            auto* bytes = static_cast<unsigned char*>(block);
            bytes[0] = static_cast<unsigned char>(i);
            blocks.push_back(block);
        }
        record_step(i, "alloc", recorder, last_pages, start, allocator);
    }

    for (size_t i = 0; i < blocks.size(); ++i) {
        allocator.deallocate(blocks[i]);
        record_step(ops + i, "free", recorder, last_pages, start, allocator);
    }
}

void run_workload_trace(Workload workload,
                        size_t ops,
                        TraceRecorder& recorder,
                        const Clock::time_point& start) {
    cma::FixedBlockAllocator<kBlockSize> allocator;

    switch (workload) {
    case Workload::Interleaved:
        run_interleaved_trace(allocator, ops, recorder, start);
        break;
    case Workload::Batch:
        run_batch_trace(allocator, ops, recorder, start);
        break;
    }
}

void write_json_uint(std::ostream& out, const char* key, size_t value, bool& first_field) {
    if (!first_field) {
        out << ',';
    }
    first_field = false;
    out << '"' << key << "\":" << value;
}

void write_json_double(std::ostream& out, const char* key, double value, bool& first_field) {
    if (!first_field) {
        out << ',';
    }
    first_field = false;
    out << '"' << key << "\":" << value;
}

void write_json_string(std::ostream& out, const char* key, const char* value, bool& first_field) {
    if (!first_field) {
        out << ',';
    }
    first_field = false;
    out << '"' << key << "\":\"" << value << '"';
}

bool write_trace_json(const TraceConfig& config,
                      double elapsed_ms,
                      const std::vector<TraceSample>& samples) {
    std::ofstream file(config.output_path);
    if (!file) {
        std::cerr << "Error: could not open '" << config.output_path << "' for writing\n";
        return false;
    }

    file << "{\n  \"meta\":{";
    bool first = true;
    write_json_string(file, "workload", workload_name(config.workload), first);
    write_json_uint(file, "ops", config.ops, first);
    write_json_uint(file, "sample_interval", config.sample_interval, first);
    write_json_uint(file, "block_size", kBlockSize, first);
    write_json_uint(file, "page_size", cma::FixedBlockAllocator<kBlockSize>::PAGE_SIZE, first);
    write_json_double(file, "elapsed_ms", elapsed_ms, first);
    file << "},\n  \"samples\":[\n";

    for (size_t i = 0; i < samples.size(); ++i) {
        const TraceSample& sample = samples[i];
        file << "    {";
        first = true;
        write_json_uint(file, "op", sample.op, first);
        write_json_string(file, "event", sample.event, first);
        write_json_double(file, "elapsed_ms", sample.elapsed_ms, first);
        write_json_uint(file, "live_blocks", sample.live_blocks, first);
        write_json_uint(file, "live_bytes", sample.live_bytes, first);
        write_json_uint(file, "active_pages", sample.active_pages, first);
        write_json_uint(file, "mapped_bytes", sample.mapped_bytes, first);
        write_json_uint(file, "free_blocks", sample.free_blocks, first);
        write_json_uint(file, "capacity_blocks", sample.capacity_blocks, first);
        file << '}';
        if (i + 1 < samples.size()) {
            file << ',';
        }
        file << '\n';
    }

    file << "  ]\n}\n";
    return true;
}

} // namespace

int run_lifecycle_trace(int argc, char* argv[]) {
    TraceConfig config;
    if (!parse_trace_config(argc, argv, config)) {
        print_trace_usage(argv[0]);
        return 1;
    }

    TraceRecorder recorder(config.sample_interval);
    const auto start = Clock::now();
    run_workload_trace(config.workload, config.ops, recorder, start);
    const auto end = Clock::now();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (!write_trace_json(config, elapsed_ms, recorder.samples())) {
        return 1;
    }

    std::cout << "Wrote " << recorder.samples().size() << " samples to " << config.output_path
              << " (" << workload_name(config.workload) << ", " << config.ops << " ops, sample "
              << config.sample_interval << ", " << elapsed_ms << " ms)\n";
    return 0;
}
