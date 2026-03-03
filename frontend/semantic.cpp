/*
 * Semantic Analysis for MiniC
 * 
 * Checks:
 * 1. Variable is declared before it is used
 * 2. Only one declaration of a variable in a scope
 * 
 * Uses "Error Poisoning" to prevent cascading errors:
 * - Variables with errors are marked as "poisoned"
 * - Subsequent uses of poisoned variables don't generate new errors
 */

#include <iostream>
#include <vector>
#include <set>
#include <string>
#include "ast.h"

using namespace std;

// Stack of symbol tables (each is a set of declared variable names)
static vector<set<string>> scopeStack;

// Set of "poisoned" variables - variables that have caused an error
// This prevents cascading errors when a variable is used after an error
static set<string> poisonedVariables;

static int errorCount = 0;

// Forward declarations
static void checkNode(astNode *node);
static void checkStmt(astNode *node);
static bool checkExpr(astNode *node);  // Returns true if expression is "poisoned"

// Push a new scope onto the stack
static void pushScope() {
    scopeStack.push_back(set<string>());
}

// Pop the current scope from the stack
static void popScope() {
    if (!scopeStack.empty()) {
        scopeStack.pop_back();
    }
}

// Check if a variable is declared in the current scope only
static bool isDeclaredInCurrentScope(const string &name) {
    if (scopeStack.empty()) return false;
    return scopeStack.back().find(name) != scopeStack.back().end();
}

// Check if a variable is declared in any scope
static bool isDeclaredInAnyScope(const string &name) {
    for (int i = scopeStack.size() - 1; i >= 0; i--) {
        if (scopeStack[i].find(name) != scopeStack[i].end()) {
            return true;
        }
    }
    return false;
}

// Declare a variable in the current scope
static void declareVariable(const string &name) {
    if (!scopeStack.empty()) {
        scopeStack.back().insert(name);
    }
}

// Check if a variable is poisoned
static bool isPoisoned(const string &name) {
    return poisonedVariables.find(name) != poisonedVariables.end();
}

// Mark a variable as poisoned
static void poisonVariable(const string &name) {
    poisonedVariables.insert(name);
}

// Check an expression node (look for variable uses)
// Returns true if the expression is "poisoned" (contains an error)
static bool checkExpr(astNode *node) {
    if (node == NULL) return false;
    
    switch (node->type) {
        case ast_var: {
            // Variable USE
            string varName(node->var.name);
            
            // If already poisoned, silently propagate the poison
            if (isPoisoned(varName)) {
                return true;  // Expression is poisoned
            }
            
            // Check if declared
            if (!isDeclaredInAnyScope(varName)) {
                cerr << "Semantic Error: Variable '" << varName << "' used before declaration" << endl;
                errorCount++;
                poisonVariable(varName);  // Poison to prevent cascade
                return true;  // Expression is poisoned
            }
            return false;  // Expression is valid
        }
        case ast_cnst:
            // Constants are always valid
            return false;
        case ast_bexpr: {
            // Binary expression: check both operands
            bool leftPoisoned = checkExpr(node->bexpr.lhs);
            bool rightPoisoned = checkExpr(node->bexpr.rhs);
            return leftPoisoned || rightPoisoned;
        }
        case ast_rexpr: {
            // Relational expression: check both operands
            bool leftPoisoned = checkExpr(node->rexpr.lhs);
            bool rightPoisoned = checkExpr(node->rexpr.rhs);
            return leftPoisoned || rightPoisoned;
        }
        case ast_uexpr:
            // Unary expression: check operand
            return checkExpr(node->uexpr.expr);
        case ast_stmt:
            // This handles cases like read() appearing as an expression
            if (node->stmt.type == ast_call) {
                // Check call argument if any
                if (node->stmt.call.param != NULL) {
                    return checkExpr(node->stmt.call.param);
                }
            }
            return false;
        default:
            return false;
    }
}

// Check a statement node
static void checkStmt(astNode *node) {
    if (node == NULL) return;
    
    if (node->type != ast_stmt) {
        // Could be nested expression or something else
        checkNode(node);
        return;
    }
    
    astStmt &stmt = node->stmt;
    
    switch (stmt.type) {
        case ast_decl: {
            // Declaration: check if already declared in current scope
            string varName(stmt.decl.name);
            if (isDeclaredInCurrentScope(varName)) {
                cerr << "Semantic Error: Variable '" << varName << "' already declared in this scope" << endl;
                errorCount++;
                poisonVariable(varName);  // Poison the variable
            } else {
                declareVariable(varName);
            }
            break;
        }
        case ast_asgn: {
            // Assignment: LHS is a variable (check if declared), RHS is an expression
            bool lhsPoisoned = false;
            if (stmt.asgn.lhs != NULL && stmt.asgn.lhs->type == ast_var) {
                string varName(stmt.asgn.lhs->var.name);
                
                // Check if LHS variable is already poisoned
                if (isPoisoned(varName)) {
                    lhsPoisoned = true;
                } else if (!isDeclaredInAnyScope(varName)) {
                    cerr << "Semantic Error: Variable '" << varName << "' used before declaration" << endl;
                    errorCount++;
                    poisonVariable(varName);
                    lhsPoisoned = true;
                }
            }
            // Check RHS expression (errors are already gated by poison check in checkExpr)
            checkExpr(stmt.asgn.rhs);
            break;
        }
        case ast_call: {
            // Function call: check argument expression
            if (stmt.call.param != NULL) {
                checkExpr(stmt.call.param);
            }
            break;
        }
        case ast_ret: {
            // Return: check the return expression
            checkExpr(stmt.ret.expr);
            break;
        }
        case ast_if: {
            // If statement: check condition and bodies
            checkExpr(stmt.ifn.cond);
            checkNode(stmt.ifn.if_body);
            if (stmt.ifn.else_body != NULL) {
                checkNode(stmt.ifn.else_body);
            }
            break;
        }
        case ast_while: {
            // While statement: check condition and body
            checkExpr(stmt.whilen.cond);
            checkNode(stmt.whilen.body);
            break;
        }
        case ast_block: {
            // Block: push new scope, check all statements, pop scope
            pushScope();
            vector<astNode*> &stmts = *(stmt.block.stmt_list);
            for (size_t i = 0; i < stmts.size(); i++) {
                checkNode(stmts[i]);
            }
            popScope();
            break;
        }
        default:
            break;
    }
}

// Check any AST node
static void checkNode(astNode *node) {
    if (node == NULL) return;
    
    switch (node->type) {
        case ast_prog: {
            // Program node: check the function
            checkNode(node->prog.func);
            break;
        }
        case ast_func: {
            // Function: push scope, add parameter if any, then process body
            // The function body block should NOT create its own scope - 
            // parameters and body declarations share the same scope
            pushScope();
            if (node->func.param != NULL && node->func.param->type == ast_var) {
                // Parameter is a declaration
                string paramName(node->func.param->var.name);
                declareVariable(paramName);
            }
            // Process function body directly without creating another scope
            // (the body is always a block in MiniC)
            if (node->func.body != NULL && node->func.body->type == ast_stmt &&
                node->func.body->stmt.type == ast_block) {
                vector<astNode*> &stmts = *(node->func.body->stmt.block.stmt_list);
                for (size_t i = 0; i < stmts.size(); i++) {
                    checkNode(stmts[i]);
                }
            } else {
                checkNode(node->func.body);
            }
            popScope();
            break;
        }
        case ast_stmt: {
            checkStmt(node);
            break;
        }
        case ast_var:
        case ast_cnst:
        case ast_bexpr:
        case ast_rexpr:
        case ast_uexpr:
            checkExpr(node);
            break;
        case ast_extern:
            // Extern declarations don't need semantic checking
            break;
        default:
            break;
    }
}

// Main entry point for semantic analysis
// Returns 0 if no errors, non-zero otherwise
int semantic_check(astNode *root) {
    scopeStack.clear();
    poisonedVariables.clear();  // Reset poisoned set
    errorCount = 0;
    
    checkNode(root);
    
    return errorCount;
}
