# MiniC Compiler

A compiler for MiniC (a subset of C) that parses source code, performs semantic analysis, generates LLVM IR, optionally optimizes it, and generates x86-32 assembly.

## Build

```
make
```

Build individual components:

```
make -C frontend        # frontend only
make -C llvm_builder    # IR builder only (standalone)
make -C optimization    # optimizer only (standalone)
make -C backend         # backend only (standalone)
```

## Usage

```
./minic <input.c> [-o <output.ll>] [-opt] [-S <output.s>]
```

| Flag | Description |
|------|-------------|
| `-o <file>` | Output LLVM IR file (default: `output.ll`) |
| `-opt` | Enable optimizations (CSE, DCE, constant folding, constant propagation) |
| `-S <file>` | Generate x86-32 assembly |

### Examples

```
./minic test_cases/t1.c -o t1.ll
./minic test_cases/t1.c -o t1.ll -opt
./minic test_cases/t1.c -o t1.ll -S t1.s
```

To run generated assembly:

```
clang -m32 t1.s test_cases/runtime.c -o t1
./t1
```

## End-to-End Tests

```
make -C test_cases test       # run all tests
make -C test_cases test-opt   # run all tests with optimization
```

## Clean

```
make clean
```
