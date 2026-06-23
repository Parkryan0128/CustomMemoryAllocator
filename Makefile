
# Compiler
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Iinclude
LDFLAGS = -pthread

DBGFLAGS = -g
RELFLAGS = -O2 -DNDEBUG

SRC_DIR = src
TEST_DIR = tests
OBJ_DIR = obj

PLATFORM_MEMORY_OBJ = $(OBJ_DIR)/PlatformMemory.o
BENCHMARK_OBJ = $(OBJ_DIR)/benchmark_main.o
UNIT_TEST_OBJS = $(OBJ_DIR)/test_main.o \
                 $(OBJ_DIR)/fixed_block_allocator_test.o \
                 $(OBJ_DIR)/platform_memory_test.o \
                 $(OBJ_DIR)/integration_test.o \
                 $(OBJ_DIR)/concurrency_test.o

BENCHMARK_TARGET = allocator_test
UNIT_TEST_TARGET = unit_tests

.PHONY: all test benchmark clean plot

all: $(BENCHMARK_TARGET) $(UNIT_TEST_TARGET)

test: $(UNIT_TEST_TARGET)
	./$(UNIT_TEST_TARGET)

benchmark: $(BENCHMARK_TARGET)
	./$(BENCHMARK_TARGET) benchmark

$(BENCHMARK_TARGET): CXXFLAGS += $(RELFLAGS)
$(BENCHMARK_TARGET): $(PLATFORM_MEMORY_OBJ) $(BENCHMARK_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

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
	rm -rf $(OBJ_DIR) $(BENCHMARK_TARGET) $(UNIT_TEST_TARGET)

plot: $(BENCHMARK_TARGET)
	@echo "--- Step 1: Running C++ benchmark to generate CSV files... ---"
	./$(BENCHMARK_TARGET) plot
	@echo "\n--- Step 2: Running Python script to generate plots... ---"
	python3 plot_results.py
	@echo "\n--- All steps completed. Plots have been generated. ---"
