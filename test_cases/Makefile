# End-to-end test Makefile for MiniC compiler
# Pipeline: .c -> minic -> .ll + .s -> clang -m32 -> executable -> run
#
# Usage:
#   make test       - build minic, generate assembly, compile, and run all tests
#   make test-opt   - same but with optimizer enabled (-opt flag)
#   make clean      - remove all generated files

MINIC = ../minic
RUNTIME = runtime.c
OUTPUT_DIR = output

# Find all test .c files (excluding runtime.c)
TEST_SRCS = $(filter-out $(RUNTIME), $(wildcard *.c))
TEST_NAMES = $(basename $(TEST_SRCS))

.PHONY: all test test-opt clean build-minic

all: test

# Ensure the minic compiler is built
build-minic:
	$(MAKE) -C ..

# Create output directory
$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

# Run all tests without optimization
test: build-minic $(OUTPUT_DIR)
	@passed=0; failed=0; total=0; \
	for src in $(TEST_SRCS); do \
		name=$$(basename $$src .c); \
		total=$$((total + 1)); \
		echo "=== Testing $$name ==="; \
		echo "[1/3] Compiling $$src -> IR + assembly..."; \
		$(MINIC) $$src -o $(OUTPUT_DIR)/$$name.ll -S $(OUTPUT_DIR)/$$name.s 2>&1; \
		if [ $$? -ne 0 ]; then \
			echo "  FAIL: minic compilation failed"; \
			failed=$$((failed + 1)); \
			continue; \
		fi; \
		echo "[2/3] Linking assembly with runtime..."; \
		clang -m32 $(OUTPUT_DIR)/$$name.s $(RUNTIME) -o $(OUTPUT_DIR)/$$name 2>&1; \
		if [ $$? -ne 0 ]; then \
			echo "  FAIL: clang linking failed"; \
			failed=$$((failed + 1)); \
			continue; \
		fi; \
		echo "[3/3] Running $$name..."; \
		$(OUTPUT_DIR)/$$name; \
		if [ $$? -ne 0 ]; then \
			echo "  FAIL: execution failed"; \
			failed=$$((failed + 1)); \
		else \
			echo "  PASS"; \
			passed=$$((passed + 1)); \
		fi; \
		echo ""; \
	done; \
	echo "========================================"; \
	echo "Results: $$passed passed, $$failed failed, $$total total"; \
	echo "========================================"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

# Run all tests with optimization
test-opt: build-minic $(OUTPUT_DIR)
	@passed=0; failed=0; total=0; \
	for src in $(TEST_SRCS); do \
		name=$$(basename $$src .c); \
		total=$$((total + 1)); \
		echo "=== Testing $$name (optimized) ==="; \
		echo "[1/3] Compiling $$src -> IR + assembly (with -opt)..."; \
		$(MINIC) $$src -o $(OUTPUT_DIR)/$$name.opt.ll -opt -S $(OUTPUT_DIR)/$$name.opt.s 2>&1; \
		if [ $$? -ne 0 ]; then \
			echo "  FAIL: minic compilation failed"; \
			failed=$$((failed + 1)); \
			continue; \
		fi; \
		echo "[2/3] Linking assembly with runtime..."; \
		clang -m32 $(OUTPUT_DIR)/$$name.opt.s $(RUNTIME) -o $(OUTPUT_DIR)/$$name.opt 2>&1; \
		if [ $$? -ne 0 ]; then \
			echo "  FAIL: clang linking failed"; \
			failed=$$((failed + 1)); \
			continue; \
		fi; \
		echo "[3/3] Running $$name (optimized)..."; \
		$(OUTPUT_DIR)/$$name.opt; \
		if [ $$? -ne 0 ]; then \
			echo "  FAIL: execution failed"; \
			failed=$$((failed + 1)); \
		else \
			echo "  PASS"; \
			passed=$$((passed + 1)); \
		fi; \
		echo ""; \
	done; \
	echo "========================================"; \
	echo "Results: $$passed passed, $$failed failed, $$total total"; \
	echo "========================================"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

clean:
	rm -rf $(OUTPUT_DIR)
