
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
UNIT_TEST_OBJS = $(OBJ_DIR)/test_main.o \
                 $(OBJ_DIR)/fixed_block_allocator_test.o \
                 $(OBJ_DIR)/platform_memory_test.o \
                 $(OBJ_DIR)/integration_test.o \
                 $(OBJ_DIR)/concurrency_test.o

BENCHMARK_TARGET = allocator_test$(SAN_SUFFIX)
UNIT_TEST_TARGET = unit_tests$(SAN_SUFFIX)

.PHONY: all test test-asan test-tsan test-ubsan benchmark clean plot

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
	rm -rf obj obj-address obj-thread obj-undefined \
		allocator_test allocator_test-address allocator_test-thread allocator_test-undefined \
		unit_tests unit_tests-address unit_tests-thread unit_tests-undefined

plot: allocator_test
	@echo "--- Step 1: Running C++ benchmark to generate CSV files... ---"
	./allocator_test plot
	@echo "\n--- Step 2: Running Python script to generate plots... ---"
	python3 plot_results.py
	@echo "\n--- All steps completed. Plots have been generated. ---"
