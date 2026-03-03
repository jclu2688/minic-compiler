# LLVM IR Optimizer

A standalone LLVM IR optimization pass that reads `.ll` files, applies local and global optimizations, and writes the optimized IR back out. Built with the LLVM-C API (LLVM 17).

## Optimizations

### Local Optimizations (per basic block)

| Pass | Description |
|------|-------------|
| **Common Subexpression Elimination (CSE)** | Detects instructions with identical opcodes and operands within a basic block and replaces redundant computations with the earlier result. Handles load instructions by checking for intervening stores to the same address. |
| **Dead Code Elimination (DCE)** | Removes instructions that produce unused values and have no side effects (excludes stores, branches, calls, returns, etc.). |
| **Constant Folding** | Evaluates arithmetic (`add`, `sub`, `mul`, `sdiv`) and comparison (`icmp`) instructions with constant operands at compile time, replacing them with the computed constant result. |

### Global Optimization (across basic blocks)

| Pass | Description |
|------|-------------|
| **Constant Propagation** | Uses iterative dataflow analysis with GEN/KILL/IN/OUT sets over store instructions to propagate constant values across basic blocks. When all reaching stores to a memory location write the same constant, the corresponding load is replaced with that constant. |

### Optimization Pipeline

The optimizer applies passes in the following order, iterating until a fixed point:

1. **CSE** on each basic block
2. **DCE** (function-wide)
3. **Constant Folding** (function-wide)
4. **DCE** again (cleanup after folding)
5. **Loop until no changes:**
   - Constant Propagation → DCE
   - Constant Folding → DCE

## Building

```bash
make          # builds the optimizer executable
make test     # builds (if needed) and runs optimizer on all test inputs
make clean    # removes executable and all test outputs
```

## Usage

```bash
./optimizer <input.ll> [output.ll]
```

- `input.ll` — the LLVM IR file to optimize
- `output.ll` — path for the optimized output

## Test Cases

See `tests/README` for details.
