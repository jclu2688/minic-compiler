#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend/ast.h"
#include "frontend/semantic.h"
#include "llvm_builder/llvm_builder.h"
#include "optimization/optimizer.h"

extern int yyparse();
extern FILE *yyin;
extern astNode *root;

void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <input_file> [-o <output_file>] [-opt]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = "output.ll";
    bool do_optimize = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-opt") == 0) {
            do_optimize = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    yyin = fopen(input_file, "r");
    if (!yyin) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", input_file);
        return 1;
    }

    // Step 1: Parse
    if (yyparse() != 0 || root == NULL) {
        fprintf(stderr, "Parsing failed.\n");
        return 1;
    }

    // Step 2: Semantic Analysis
    int semanticErrors = semantic_check(root);
    if (semanticErrors > 0) {
        fprintf(stderr, "Semantic analysis found %d error(s). IR generation aborted.\n", semanticErrors);
        freeNode(root);
        return 1;
    }

    // Step 3: Preprocess (Variable Renaming)
    renameVariables(root);

    // Step 3: IR Generation
    LLVMModuleRef mod = generateIR(root);
    if (!mod) {
        fprintf(stderr, "IR generation failed.\n");
        return 1;
    }

    // Step 4: Optimization
    if (do_optimize) {
        for (LLVMValueRef function = LLVMGetFirstFunction(mod);
             function;
             function = LLVMGetNextFunction(function)) {
            optimizeFunction(function);
        }
    }

    // Step 5: Output
    LLVMPrintModuleToFile(mod, output_file, NULL);
    printf("Successfully generated LLVM IR: %s\n", output_file);

    // Cleanup
    LLVMDisposeModule(mod);
    freeNode(root);
    fclose(yyin);

    return 0;
}
