COMPILER_NAME = jsastac

CC = cc
CFLAGS = -Wall -Wextra -g -Isrc $(shell llvm-config --cflags)
LDFLAGS = $(shell llvm-config --ldflags --libs core)
LDLIBS = $(shell llvm-config --system-libs)

# Directories
SRC_DIR = src
BUILD_DIR = build

TARGET = $(BUILD_DIR)/$(COMPILER_NAME)
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $(TARGET)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) *.ll

test: $(TARGET)
	$(TARGET) test_basic.js $(BUILD_DIR)/test_basic.ll
	$(TARGET) test_loops.js $(BUILD_DIR)/test_loops.ll
	$(TARGET) test_functions.js $(BUILD_DIR)/test_functions.ll
	$(TARGET) test_advanced.js $(BUILD_DIR)/test_advanced.ll

run_basic: $(TARGET)
	$(TARGET) test_basic.js $(BUILD_DIR)/test_basic.ll
	lli $(BUILD_DIR)/test_basic.ll

run_loops: $(TARGET)
	$(TARGET) test_loops.js $(BUILD_DIR)/test_loops.ll
	lli $(BUILD_DIR)/test_loops.ll

run_functions: $(TARGET)
	$(TARGET) test_functions.js $(BUILD_DIR)/test_functions.ll
	lli $(BUILD_DIR)/test_functions.ll

run_advanced: $(TARGET)
	$(TARGET) test_advanced.js $(BUILD_DIR)/test_advanced.ll
	lli $(BUILD_DIR)/test_advanced.ll

# Install to system (optional)
install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/jscompiler

# Uninstall from system
uninstall:
	rm -f /usr/local/bin/jscompiler

.PHONY: all clean test run_basic run_loops run_functions run_advanced install uninstall
