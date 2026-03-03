#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "llvm_builder.h"

extern int yyparse();
extern FILE *yyin;
extern astNode *root;

#ifndef MAIN_UNIFIED
void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <input_file> [-o <output_file>]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = "test.ll";

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
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

    if (yyparse() != 0 || root == NULL) {
        fprintf(stderr, "Parsing failed.\n");
        return 1;
    }

    // Part 1: Rename variables
    renameVariables(root);

    // Part 2: Generate IR
    LLVMModuleRef mod = generateIR(root);
    if (!mod) {
        fprintf(stderr, "IR generation failed.\n");
        return 1;
    }

    // Output IR
    LLVMPrintModuleToFile(mod, output_file, NULL);
    LLVMDumpModule(mod);

    // Cleanup
    LLVMDisposeModule(mod);
    freeNode(root);
    fclose(yyin);

    return 0;
}
#endif
