CXX = g++
LLVM_CONFIG = llvm-config-17

CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags) -Wall -DSTANDALONE_BACKEND -std=c++11
LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --libs core irreader)

OUTPUT_DIR = tests/output

# Find all test inputs
TEST_INPUTS = $(wildcard tests/*.ll)
TEST_OUTPUTS = $(patsubst tests/%.ll,$(OUTPUT_DIR)/%.s,$(TEST_INPUTS))

all: backend

backend: backend.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Generate .ll files from all .c test files in assembly_gen_tests/
generate-ll:
	@mkdir -p tests
	@for f in assembly_gen_tests/*.c; do \
		base=$$(basename $$f .c); \
		if [ "$$base" != "main" ]; then \
			echo "Generating tests/$$base.ll from $$f"; \
			clang -S -emit-llvm -m32 -O0 -o tests/$$base.ll $$f 2>/dev/null || echo "  Warning: clang -m32 failed for $$f"; \
		fi \
	done

test: backend $(TEST_OUTPUTS)

$(OUTPUT_DIR)/%.s: tests/%.ll backend | $(OUTPUT_DIR)
	./backend $< $@

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

# Assemble and run each test
test-run: test
	@for s in $(OUTPUT_DIR)/*.s; do \
		base=$$(basename $$s .s); \
		echo "=== Testing $$base ==="; \
		clang -m32 $$s assembly_gen_tests/main.c -o /tmp/test_$$base 2>/dev/null && /tmp/test_$$base || echo "  Failed to build/run $$base"; \
	done

clean:
	rm -f backend
	rm -f *.s
	rm -f *.out
	rm -rf $(OUTPUT_DIR)

.PHONY: all test test-run clean generate-ll
