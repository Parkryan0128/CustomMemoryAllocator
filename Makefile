
# Compiler
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Iinclude
LDFLAGS = -pthread

DBGFLAGS = -g
RELFLAGS = -O2 -DNDEBUG

# Optional sanitizer build: make test SANITIZE=address|thread|undefined
SANITIZE ?=
SAN_SUFFIX := $(if $(SANITIZE),-$(SANITIZE),)

ifeq ($(SANITIZE),address)
  SANFLAGS = -fsanitize=address -fno-omit-frame-pointer
else ifeq ($(SANITIZE),thread)
  SANFLAGS = -fsanitize=thread -fno-omit-frame-pointer
  DBGFLAGS = -g -O1
  CXXFLAGS += -DCMA_TSAN_BUILD
else ifeq ($(SANITIZE),undefined)
  SANFLAGS = -fsanitize=undefined -fno-omit-frame-pointer
else ifneq ($(SANITIZE),)
  $(error Unknown SANITIZE=$(SANITIZE). Use address, thread, or undefined.)
endif

CXXFLAGS += $(SANFLAGS)
LDFLAGS += $(SANFLAGS)

SRC_DIR = src
TEST_DIR = tests
OBJ_DIR = obj$(SAN_SUFFIX)

PLATFORM_MEMORY_OBJ = $(OBJ_DIR)/PlatformMemory.o
BENCHMARK_OBJ = $(OBJ_DIR)/benchmark_main.o
LIFECYCLE_TRACE_OBJ = $(OBJ_DIR)/lifecycle_trace.o
ALLOCATOR_CLI_OBJ = $(OBJ_DIR)/allocator_cli_main.o
UNIT_TEST_OBJS = $(OBJ_DIR)/test_main.o \
                 $(OBJ_DIR)/fixed_block_allocator_test.o \
                 $(OBJ_DIR)/platform_memory_test.o \
                 $(OBJ_DIR)/integration_test.o \
                 $(OBJ_DIR)/concurrency_test.o

BENCHMARK_TARGET = allocator_test$(SAN_SUFFIX)
UNIT_TEST_TARGET = unit_tests$(SAN_SUFFIX)

.PHONY: all test test-asan test-tsan test-ubsan benchmark dashboard clean plot

all: $(BENCHMARK_TARGET) $(UNIT_TEST_TARGET)

test: $(UNIT_TEST_TARGET)
	./$(UNIT_TEST_TARGET)

test-asan:
	$(MAKE) test SANITIZE=address

test-tsan:
	$(MAKE) test SANITIZE=thread

test-ubsan:
	$(MAKE) test SANITIZE=undefined

benchmark: $(BENCHMARK_TARGET)
	./$(BENCHMARK_TARGET) benchmark

dashboard: $(BENCHMARK_TARGET)
	@mkdir -p dashboard/data
	@echo "--- Step 1: Benchmark CSV ---"
	./$(BENCHMARK_TARGET) plot
	@echo "\n--- Step 2: Lifecycle traces ---"
	./$(BENCHMARK_TARGET) trace --workload interleaved --ops 100000 --sample 2000 --out dashboard/data/lifecycle_trace_interleaved.json
	./$(BENCHMARK_TARGET) trace --workload batch --ops 50000 --sample 1000 --out dashboard/data/lifecycle_trace_batch.json
	@echo "\n--- Step 3: HTML dashboard ---"
	python3 dashboard/generate.py
	@echo "\nOpen index.html locally, or see the live site on GitHub Pages (README)."

$(BENCHMARK_TARGET): CXXFLAGS += $(RELFLAGS)
$(BENCHMARK_TARGET): $(PLATFORM_MEMORY_OBJ) $(BENCHMARK_OBJ) $(LIFECYCLE_TRACE_OBJ) $(ALLOCATOR_CLI_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/benchmark_main.o: CXXFLAGS += -DCMA_NO_MAIN

$(UNIT_TEST_TARGET): CXXFLAGS += $(DBGFLAGS)
$(UNIT_TEST_TARGET): $(PLATFORM_MEMORY_OBJ) $(UNIT_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<


clean:
	rm -rf obj obj-address obj-thread obj-undefined \
		allocator_test allocator_test-address allocator_test-thread allocator_test-undefined \
		unit_tests unit_tests-address unit_tests-thread unit_tests-undefined

plot: $(BENCHMARK_TARGET)
	@mkdir -p dashboard/data
	@echo "--- Step 1: Running C++ benchmark to generate CSV... ---"
	./$(BENCHMARK_TARGET) plot
	@echo "\n--- Step 2: Running Python script to generate plots... ---"
	python3 dashboard/plot_results.py
	@echo "\n--- All steps completed. Output is in dashboard/data/. ---"
