CC = gcc
LLVM_CONFIG = llvm-config-17

CFLAGS = $(shell $(LLVM_CONFIG) --cflags) -Wall -DSTANDALONE_OPTIMIZER
LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --libs core irreader)

OUTPUT_DIR = tests/output

# Find all test inputs (exclude reference _opt.ll files)
TEST_INPUTS = $(filter-out %_opt.ll, $(wildcard tests/*.ll))
TEST_OUTPUTS = $(patsubst tests/%.ll,$(OUTPUT_DIR)/%_opt.ll,$(TEST_INPUTS))

all: optimizer

optimizer: optimizer.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: optimizer $(TEST_OUTPUTS)

$(OUTPUT_DIR)/%_opt.ll: tests/%.ll optimizer | $(OUTPUT_DIR)
	./optimizer $< $@

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

clean:
	rm -f optimizer
	rm -rf $(OUTPUT_DIR)

.PHONY: all test clean
