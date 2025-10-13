
# Compiler
CXX = g++
CXXFLAGS = -std=c++17 -g -Wall -Iinclude

# Directories
SRC_DIR = src
TEST_DIR = tests
OBJ_DIR = obj

# Source files and Object files
SRCS = $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(TEST_DIR)/*.cpp)
OBJS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(notdir $(SRCS)))

# Target executable
TARGET = allocator_test

# Default rule
all: $(TARGET)

# Link object files to create the target
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Clean up build files
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

plot: all
	@echo "--- Step 1: Running C++ benchmark to generate CSV files... ---"
	./$(TARGET) plot
	@echo "\n--- Step 2: Running Python script to generate plots... ---"
	python3 plot_results.py
	@echo "\n--- All steps completed. Plots have been generated. ---"

.PHONY: all clean