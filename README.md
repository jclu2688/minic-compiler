# MiniC Compiler

A compiler for MiniC (a subset of C) that parses source code, performs semantic analysis, generates LLVM IR, and optionally optimizes it.

## Build

Build the full compiler:

```
make
```

Build individual components:

```
make -C frontend        # frontend only
make -C llvm_builder    # IR builder only (standalone)
make -C optimization    # optimizer only (standalone)
```

## Usage

```
./minic <input.c> [-o <output.ll>] [-opt]
```

See subfolders' READMEs for more details about usage for individual components.

| Flag | Description |
|------|-------------|
| `-o <file>` | Output file (default: `output.ll`) |
| `-opt` | Enable optimizations (CSE, DCE, constant folding, constant propagation) |

### Examples

```
./minic tests/t1.c -o t1.ll
./minic tests/t1.c -o t1_opt.ll -opt
```

## Clean

```
make clean
```
