/*
 * MiniC Compiler Frontend
 * Part 1: Syntax Analysis (Flex/Bison)
 * Part 2: Semantic Analysis
 */

#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "semantic.h"

extern int yyparse();
extern FILE *yyin;
extern astNode *root;

int main(int argc, char *argv[]) {
    // If a file is provided, use it as input
    if (argc > 1) {
        yyin = fopen(argv[1], "r");
        if (!yyin) {
            fprintf(stderr, "Error: Cannot open file '%s'\n", argv[1]);
            return 1;
        }
    }
    
    // Part 1: Parse the input and build the AST
    printf("=== Part 1: Syntax Analysis ===\n");
    int parseResult = yyparse();
    
    if (parseResult != 0) {
        fprintf(stderr, "Parsing failed.\n");
        if (yyin && yyin != stdin) fclose(yyin);
        return 1;
    }
    
    if (root == NULL) {
        fprintf(stderr, "Error: No AST was generated.\n");
        if (yyin && yyin != stdin) fclose(yyin);
        return 1;
    }
    
    printf("Parsing successful! AST created.\n");
    printf("\nAST Structure:\n");
    printNode(root);
    
    // Part 2: Semantic Analysis
    printf("\n=== Part 2: Semantic Analysis ===\n");
    int semanticErrors = semantic_check(root);
    
    if (semanticErrors > 0) {
        printf("Semantic analysis found %d error(s).\n", semanticErrors);
    } else {
        printf("Semantic analysis passed. No errors found.\n");
    }
    
    // Cleanup
    freeNode(root);
    if (yyin && yyin != stdin) fclose(yyin);
    
    return semanticErrors > 0 ? 1 : 0;
}
