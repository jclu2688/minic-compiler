#ifndef LLVM_BUILDER_H
#define LLVM_BUILDER_H

#include <llvm-c/Core.h>
#include "ast.h"

// Rename all variables in the AST to have unique names
void renameVariables(astNode* root);

// Generate LLVM IR from the AST
// Returns an LLVMModuleRef representing the program
LLVMModuleRef generateIR(astNode* root);

#endif
