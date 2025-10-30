# Compiler and LSP daemon names
COMPILER_NAME = jsastac
LSP_NAME = jsastad

CC = cc

# DEBUG mode: make DEBUG=1
# Release mode: make (default)
ifdef DEBUG
	OPT_FLAGS = -O0 -g -fsanitize=address
	DEBUG_INFO = Debug build with sanitizers enabled
else
	OPT_FLAGS = -O2 -g
	DEBUG_INFO = Release build
endif

# Common flags
CFLAGS = -Wall -Wextra $(OPT_FLAGS) -Isrc $(shell llvm-config --cflags)
LDFLAGS = $(shell llvm-config --ldflags --libs core)
LDLIBS = $(shell llvm-config --system-libs)

ifdef DEBUG
	LDFLAGS += -fsanitize=address
endif

# Directories
COMMON_DIR = src/common
COMPILER_DIR = src/compiler
LSP_DIR = src/lsp
BUILD_DIR = build

# Targets
COMPILER_TARGET = $(BUILD_DIR)/$(COMPILER_NAME)
LSP_TARGET = $(BUILD_DIR)/$(LSP_NAME)

# Common sources (everything in common directory)
COMMON_SOURCES = $(wildcard $(COMMON_DIR)/*.c)
COMMON_OBJECTS = $(COMMON_SOURCES:$(COMMON_DIR)/%.c=$(BUILD_DIR)/common/%.o)
COMMON_HEADERS = $(wildcard $(COMMON_DIR)/*.h)

# Compiler sources
COMPILER_SOURCES = $(wildcard $(COMPILER_DIR)/*.c)
COMPILER_OBJECTS = $(COMPILER_SOURCES:$(COMPILER_DIR)/%.c=$(BUILD_DIR)/compiler/%.o)
COMPILER_HEADERS = $(wildcard $(COMPILER_DIR)/*.h)

# LSP sources (exclude test_code_index.c)
LSP_SOURCES = $(filter-out $(LSP_DIR)/test_code_index.c, $(wildcard $(LSP_DIR)/*.c))
LSP_OBJECTS = $(LSP_SOURCES:$(LSP_DIR)/%.c=$(BUILD_DIR)/lsp/%.o)
LSP_HEADERS = $(wildcard $(LSP_DIR)/*.h)

.PHONY: all compiler lsp clean test install uninstall help test_code_index

# Default target: build both binaries
all: info compiler lsp

# Build only compiler
compiler: $(COMPILER_TARGET)

# Build only LSP daemon
lsp: $(LSP_TARGET)

# Show build configuration
info:
	@echo "================================================"
	@echo "  JSasta Build Configuration"
	@echo "================================================"
	@echo "  Mode:           $(DEBUG_INFO)"
	@echo "  Optimization:   $(OPT_FLAGS)"
	@echo "  Compiler:       $(COMPILER_TARGET)"
	@echo "  LSP Daemon:     $(LSP_TARGET)"
	@echo "================================================"
	@echo

# Create build directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/common $(BUILD_DIR)/compiler $(BUILD_DIR)/lsp

# Compiler binary
$(COMPILER_TARGET): $(COMMON_OBJECTS) $(COMPILER_OBJECTS) | $(BUILD_DIR)
	@echo "Linking compiler: $(COMPILER_TARGET)"
	$(CC) $(COMMON_OBJECTS) $(COMPILER_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $(COMPILER_TARGET)
	@echo "Built: $(COMPILER_TARGET)"

# LSP daemon binary
$(LSP_TARGET): $(COMMON_OBJECTS) $(LSP_OBJECTS) | $(BUILD_DIR)
	@echo "Linking LSP daemon: $(LSP_TARGET)"
	$(CC) $(COMMON_OBJECTS) $(LSP_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $(LSP_TARGET)
	@echo "Built: $(LSP_TARGET)"

# Test code index
test_code_index: $(BUILD_DIR)/test_code_index
	@echo "Running code index test..."
	$(BUILD_DIR)/test_code_index test_code_index.jsa

$(BUILD_DIR)/test_code_index: $(COMMON_OBJECTS) $(BUILD_DIR)/lsp/code_index.o $(BUILD_DIR)/lsp/test_code_index.o | $(BUILD_DIR)
	@echo "Linking test_code_index"
	$(CC) $(COMMON_OBJECTS) $(BUILD_DIR)/lsp/code_index.o $(BUILD_DIR)/lsp/test_code_index.o $(LDFLAGS) $(LDLIBS) -o $(BUILD_DIR)/test_code_index

$(BUILD_DIR)/lsp/test_code_index.o: $(LSP_DIR)/test_code_index.c $(COMMON_HEADERS) $(LSP_HEADERS) | $(BUILD_DIR)
	@echo "Compiling test_code_index"
	$(CC) $(CFLAGS) -c $(LSP_DIR)/test_code_index.c -o $(BUILD_DIR)/lsp/test_code_index.o

# Compile common objects (depend on all common headers)
$(BUILD_DIR)/common/%.o: $(COMMON_DIR)/%.c $(COMMON_HEADERS) | $(BUILD_DIR)
	@echo "Compiling [common]: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Compile compiler objects (depend on common headers and compiler headers)
$(BUILD_DIR)/compiler/%.o: $(COMPILER_DIR)/%.c $(COMMON_HEADERS) $(COMPILER_HEADERS) | $(BUILD_DIR)
	@echo "Compiling [compiler]: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Compile LSP objects (depend on common headers and LSP headers)
$(BUILD_DIR)/lsp/%.o: $(LSP_DIR)/%.c $(COMMON_HEADERS) $(LSP_HEADERS) | $(BUILD_DIR)
	@echo "Compiling [lsp]: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) *.ll
	@echo "Cleaned build directory"

# Test targets (using compiler)
test: $(COMPILER_TARGET)
	@echo "Running tests..."
	$(COMPILER_TARGET) test_basic.js $(BUILD_DIR)/test_basic.ll
	$(COMPILER_TARGET) test_loops.js $(BUILD_DIR)/test_loops.ll
	$(COMPILER_TARGET) test_functions.js $(BUILD_DIR)/test_functions.ll
	$(COMPILER_TARGET) test_advanced.js $(BUILD_DIR)/test_advanced.ll
	@echo "All tests passed!"

run_basic: $(COMPILER_TARGET)
	$(COMPILER_TARGET) test_basic.js $(BUILD_DIR)/test_basic.ll
	lli $(BUILD_DIR)/test_basic.ll

run_loops: $(COMPILER_TARGET)
	$(COMPILER_TARGET) test_loops.js $(BUILD_DIR)/test_loops.ll
	lli $(BUILD_DIR)/test_loops.ll

run_functions: $(COMPILER_TARGET)
	$(COMPILER_TARGET) test_functions.js $(BUILD_DIR)/test_functions.ll
	lli $(BUILD_DIR)/test_functions.ll

run_advanced: $(COMPILER_TARGET)
	$(COMPILER_TARGET) test_advanced.js $(BUILD_DIR)/test_advanced.ll
	lli $(BUILD_DIR)/test_advanced.ll

# Install to system
install: $(COMPILER_TARGET) $(LSP_TARGET)
	@echo "Installing binaries to /usr/local/bin..."
	install -m 755 $(COMPILER_TARGET) /usr/local/bin/$(COMPILER_NAME)
	install -m 755 $(LSP_TARGET) /usr/local/bin/$(LSP_NAME)
	@echo "Installed: $(COMPILER_NAME) and $(LSP_NAME)"

# Uninstall from system
uninstall:
	@echo "Uninstalling binaries from /usr/local/bin..."
	rm -f /usr/local/bin/$(COMPILER_NAME)
	rm -f /usr/local/bin/$(LSP_NAME)
	@echo "Uninstalled: $(COMPILER_NAME) and $(LSP_NAME)"

# Help message
help:
	@echo "JSasta Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build both compiler and LSP daemon (default)"
	@echo "  compiler     - Build only the compiler (jsastac)"
	@echo "  lsp          - Build only the LSP daemon (jsastad)"
	@echo "  clean        - Remove all build artifacts"
	@echo "  test         - Run all test files"
	@echo "  install      - Install binaries to /usr/local/bin"
	@echo "  uninstall    - Remove installed binaries"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Build modes:"
	@echo "  make             - Release build with -O2"
	@echo "  make DEBUG=1     - Debug build with -O0, debug symbols, and address sanitizer"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build both in release mode"
	@echo "  make DEBUG=1            # Build both in debug mode"
	@echo "  make compiler           # Build only compiler in release mode"
	@echo "  make DEBUG=1 compiler   # Build only compiler in debug mode"
	@echo "  make clean all          # Clean and rebuild everything"
