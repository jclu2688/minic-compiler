# MiniC Compiler Frontend

A compiler frontend for MiniC (a subset of C) implementing syntax analysis and semantic analysis.

## Overview

This project consists of two main parts:

1. **Part 1: Syntax Analysis** - Uses Flex (lexer) and Bison (parser) to parse MiniC programs and build an Abstract Syntax Tree (AST)
2. **Part 2: Semantic Analysis** - Traverses the AST to check for semantic errors

## Building

```bash
make
```

## Usage

```bash
./frontend <input_file.c>
```

---

## Implementation Details

### File Structure

| File | Description |
|------|-------------|
| `ast.h` / `ast.c` | AST node definitions and create/free functions (provided, not modified) |
| `minic.l` | Flex lexer - tokenizes MiniC source code |
| `minic.y` | Bison parser - builds AST using `ast.h` functions |
| `semantic.h` / `semantic.cpp` | Semantic analyzer |
| `main.cpp` | Entry point - runs parser then semantic analysis |
| `Makefile` | Build configuration |

### Part 1: Syntax Analysis

The parser (`minic.y`) constructs the AST using the provided `ast.h` API:
- `createProg()` - Program root with extern declarations and function
- `createFunc()` - Function with name, optional parameter, and body
- `createBlock()` - Block statement with list of statements (`vector<astNode*>`)
- `createDecl()`, `createAsgn()`, `createIf()`, `createWhile()`, `createRet()`, `createCall()`
- `createVar()`, `createCnst()` - Variables and constants
- `createBExpr()`, `createRExpr()`, `createUExpr()` - Expressions

Incorrect syntax will result in a syntax error.

### Part 2: Semantic Analysis

The semantic analyzer performs a **depth-first traversal** of the AST and checks:

1. **Variable declared before use** - Every variable must be declared before it appears in an expression or assignment
2. **Single declaration per scope** - No duplicate declarations in the same scope

---

## Error Handling: Poisoning Strategy

### The Problem: Error Cascades

Without careful handling, one error can cause many "noise" errors:

```c
int main() {
    // x is never declared
    y = x + 5;    // Error: 'x' undeclared
    print(x);     // Error: 'x' undeclared (noise)
    return x;     // Error: 'x' undeclared (noise)
}
```

### The Solution: Symbol Poisoning

When a variable causes an error, it is added to a **poisoned set**:

```cpp
set<string> poisonedVariables;
```

**Rules:**
1. When an undeclared or multiply-declared variable is found → report error, mark as poisoned
2. When checking an expression, if a variable is already poisoned → **stay silent**
3. The poison propagates through expressions (if any operand is poisoned, the result is poisoned)

**Key Assumption: Global poisoning** - Once a variable name is poisoned, it stays poisoned for the entire program.

**Result:** The example above produces only **1 error** instead of 3.

---

## Example Output

```
$ ./frontend test1_valid.c
=== Part 1: Syntax Analysis ===
Parsing successful! AST created.

AST Structure:
Prog:
 Func: main
  Stmt: 
   Block:
    ...

=== Part 2: Semantic Analysis ===
Semantic analysis passed. No errors found.
```

```
$ ./frontend test6_cascade.c
=== Part 1: Syntax Analysis ===
Parsing successful! AST created.
...

=== Part 2: Semantic Analysis ===
Semantic Error: Variable 'x' used before declaration
Semantic analysis found 1 error(s).
```

---

## Assumptions

1. **All declarations at block start** - MiniC requires declarations before any statements in a block
2. **Single-pass analysis** - The semantic analyzer makes one traversal of the AST
3. **No type checking** - Since all values are `int`, no type errors are possible (other than declaration errors)
4. **Nested block shadowing is allowed** - A variable can be redeclared in a nested block
5. **Global poisoning** - Once a variable name is poisoned, it stays poisoned for the entire file (simplifies implementation)
