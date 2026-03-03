#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

// Perform semantic analysis on the AST
// Returns 0 if no errors, number of errors otherwise
int semantic_check(astNode *root);

#endif
