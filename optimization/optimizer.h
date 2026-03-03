#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <llvm-c/Core.h>

#ifdef __cplusplus
extern "C" {
#endif

// Main entry point for optimizing a single LLVM function.
// Applies CSE, DCE, Constant Folding, and Global Constant Propagation.
void optimizeFunction(LLVMValueRef function);

#ifdef __cplusplus
}
#endif

#endif
