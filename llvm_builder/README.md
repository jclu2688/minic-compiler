# MiniC LLVM IR Builder

Generates LLVM IR from MiniC source files.

## Build

```
make
```

## Usage

```
./llvm_builder <input.c> [-o <output.ll>]
```

If `-o` is not specified, the output is written to `test.ll`.

### Examples

```
./llvm_builder tests/p1.c -o p1_out.ll
./llvm_builder tests/p3.c
```

## Test cases

| File | Description |
|------|-------------|
| tests/p1.c | Basic arithmetic with parameter |
| tests/p2.c | If/else with nested while loop |
| tests/p3.c | While loop with print() calls |
| tests/p4.c | Early return, read() in while loop |
| tests/p5.c | If inside while with read() |
| tests/p6.c | Same as p5 |

Each `.c` file has a corresponding `.ll` file showing the expected clang output for reference.

## Clean

```
make clean
```
