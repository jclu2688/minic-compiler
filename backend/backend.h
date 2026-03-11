#ifndef BACKEND_H
#define BACKEND_H

#include <llvm-c/Core.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generate x86-32 assembly from an LLVM module, writing to outputFile.
void generateAssembly(LLVMModuleRef mod, const char *outputFile);

#ifdef __cplusplus
}
#endif

#endif
