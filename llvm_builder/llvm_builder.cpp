#include "llvm_builder.h"
#include <llvm-c/Core.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <queue>

using namespace std;

// Global state for IR generation (per-function scope)
static LLVMBuilderRef builder;
static LLVMValueRef ret_ref;           // alloca for return value
static LLVMBasicBlockRef retBB;        // return basic block
static map<string, LLVMValueRef> var_map; // variable name -> alloca
static LLVMValueRef printFunc;
static LLVMValueRef readFunc;
static LLVMTypeRef printFuncType;
static LLVMTypeRef readFuncType;

// Forward declarations
static LLVMBasicBlockRef genIRStmt(astNode* node, LLVMBasicBlockRef startBB);
static LLVMValueRef genIRExpr(astNode* node);

// Variable Renaming
// Collect all variable names declared in a block (including nested blocks)
// and rename them to unique names using a counter
static int renameCounter = 0;

static void renameStmt(astNode* node, map<string, string>& nameMap);
static void renameExpr(astNode* node, map<string, string>& nameMap);

static void renameExpr(astNode* node, map<string, string>& nameMap) {
    if (node == NULL) return;

    switch (node->type) {
        case ast_var: {
            string oldName(node->var.name);
            if (nameMap.find(oldName) != nameMap.end()) {
                free(node->var.name);
                node->var.name = strdup(nameMap[oldName].c_str());
            }
            break;
        }
        case ast_cnst:
            break;
        case ast_bexpr:
            renameExpr(node->bexpr.lhs, nameMap);
            renameExpr(node->bexpr.rhs, nameMap);
            break;
        case ast_rexpr:
            renameExpr(node->rexpr.lhs, nameMap);
            renameExpr(node->rexpr.rhs, nameMap);
            break;
        case ast_uexpr:
            renameExpr(node->uexpr.expr, nameMap);
            break;
        case ast_stmt:
            if (node->stmt.type == ast_call) {
                // read() call used as expression
                // param might need renaming
                renameExpr(node->stmt.call.param, nameMap);
            }
            break;
        default:
            break;
    }
}

static void renameStmt(astNode* node, map<string, string>& nameMap) {
    if (node == NULL) return;
    if (node->type != ast_stmt) return;

    switch (node->stmt.type) {
        case ast_decl: {
            string oldName = node->stmt.decl.name;
            string newName = oldName + "_" + to_string(renameCounter++);
            nameMap[oldName] = newName;
            free(node->stmt.decl.name);
            node->stmt.decl.name = strdup(newName.c_str());
            break;
        }
        case ast_asgn:
            renameExpr(node->stmt.asgn.lhs, nameMap);
            renameExpr(node->stmt.asgn.rhs, nameMap);
            break;
        case ast_call:
            renameExpr(node->stmt.call.param, nameMap);
            break;
        case ast_ret:
            renameExpr(node->stmt.ret.expr, nameMap);
            break;
        case ast_while:
            renameExpr(node->stmt.whilen.cond, nameMap);
            renameStmt(node->stmt.whilen.body, nameMap);
            break;
        case ast_if:
            renameExpr(node->stmt.ifn.cond, nameMap);
            renameStmt(node->stmt.ifn.if_body, nameMap);
            renameStmt(node->stmt.ifn.else_body, nameMap);
            break;
        case ast_block: {
            // Create a copy of the current name map for this scope
            map<string, string> scopeMap(nameMap);
            vector<astNode*>* stmts = node->stmt.block.stmt_list;
            for (size_t i = 0; i < stmts->size(); i++) {
                renameStmt((*stmts)[i], scopeMap);
            }
            break;
        }
        default:
            break;
    }
}

void renameVariables(astNode* root) {
    if (root == NULL || root->type != ast_prog) return;

    astNode* funcNode = root->prog.func;
    if (funcNode == NULL || funcNode->type != ast_func) return;

    renameCounter = 0;
    map<string, string> nameMap;

    // Rename the parameter if it exists
    if (funcNode->func.param != NULL) {
        string oldName = funcNode->func.param->var.name;
        string newName = oldName + "_" + to_string(renameCounter++);
        nameMap[oldName] = newName;
        free(funcNode->func.param->var.name);
        funcNode->func.param->var.name = strdup(newName.c_str());
    }

    // Rename variables in the function body
    renameStmt(funcNode->func.body, nameMap);
}

// Helper: collect all declared variable names from the function body
static void collectDeclNames(astNode* node, set<string>& names) {
    if (node == NULL) return;
    if (node->type != ast_stmt) return;

    switch (node->stmt.type) {
        case ast_decl:
            names.insert(node->stmt.decl.name);
            break;
        case ast_block: {
            vector<astNode*>* stmts = node->stmt.block.stmt_list;
            for (size_t i = 0; i < stmts->size(); i++) {
                collectDeclNames((*stmts)[i], names);
            }
            break;
        }
        case ast_while:
            collectDeclNames(node->stmt.whilen.body, names);
            break;
        case ast_if:
            collectDeclNames(node->stmt.ifn.if_body, names);
            collectDeclNames(node->stmt.ifn.else_body, names);
            break;
        default:
            break;
    }
}

// Dead basic block removal via BFS from entry
static void removeDeadBlocks(LLVMValueRef function) {
    // Collect all basic blocks
    vector<LLVMBasicBlockRef> allBlocks;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        allBlocks.push_back(bb);
    }

    if (allBlocks.empty()) return;

    // BFS from entry block to find reachable blocks
    set<LLVMBasicBlockRef> reachable;
    queue<LLVMBasicBlockRef> worklist;

    LLVMBasicBlockRef entryBlock = allBlocks[0];
    worklist.push(entryBlock);
    reachable.insert(entryBlock);

    while (!worklist.empty()) {
        LLVMBasicBlockRef bb = worklist.front();
        worklist.pop();

        LLVMValueRef term = LLVMGetBasicBlockTerminator(bb);
        if (term == NULL) continue;

        unsigned numSucc = LLVMGetNumSuccessors(term);
        for (unsigned i = 0; i < numSucc; i++) {
            LLVMBasicBlockRef succ = LLVMGetSuccessor(term, i);
            if (reachable.find(succ) == reachable.end()) {
                reachable.insert(succ);
                worklist.push(succ);
            }
        }
    }

    // Delete unreachable blocks
    for (size_t i = 0; i < allBlocks.size(); i++) {
        if (reachable.find(allBlocks[i]) == reachable.end()) {
            LLVMDeleteBasicBlock(allBlocks[i]);
        }
    }
}

// genIRExpr: Generate LLVM IR for an expression node
static LLVMValueRef genIRExpr(astNode* node) {
    if (node == NULL) return NULL;

    switch (node->type) {
        case ast_cnst: {
            return LLVMConstInt(LLVMInt32Type(), (unsigned long long)node->cnst.value, 1);
        }
        case ast_var: {
            string name = node->var.name;
            LLVMValueRef alloca_ref = var_map[name];
            return LLVMBuildLoad2(builder, LLVMInt32Type(), alloca_ref, name.c_str());
        }
        case ast_uexpr: {
            LLVMValueRef exprVal = genIRExpr(node->uexpr.expr);
            LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
            return LLVMBuildSub(builder, zero, exprVal, "negtmp");
        }
        case ast_bexpr: {
            LLVMValueRef lhs = genIRExpr(node->bexpr.lhs);
            LLVMValueRef rhs = genIRExpr(node->bexpr.rhs);
            switch (node->bexpr.op) {
                case add:
                    return LLVMBuildAdd(builder, lhs, rhs, "addtmp");
                case sub:
                    return LLVMBuildSub(builder, lhs, rhs, "subtmp");
                case mul:
                    return LLVMBuildMul(builder, lhs, rhs, "multmp");
                case divide:
                    return LLVMBuildSDiv(builder, lhs, rhs, "divtmp");
                default:
                    return NULL;
            }
        }
        case ast_rexpr: {
            LLVMValueRef lhs = genIRExpr(node->rexpr.lhs);
            LLVMValueRef rhs = genIRExpr(node->rexpr.rhs);
            LLVMIntPredicate pred;
            switch (node->rexpr.op) {
                case lt:  pred = LLVMIntSLT; break;
                case gt:  pred = LLVMIntSGT; break;
                case le:  pred = LLVMIntSLE; break;
                case ge:  pred = LLVMIntSGE; break;
                case eq:  pred = LLVMIntEQ;  break;
                case neq: pred = LLVMIntNE;  break;
                default:  pred = LLVMIntEQ;  break;
            }
            return LLVMBuildICmp(builder, pred, lhs, rhs, "cmptmp");
        }
        case ast_stmt: {
            // This handles read() used as an expression
            if (node->stmt.type == ast_call) {
                LLVMValueRef callResult = LLVMBuildCall2(builder, readFuncType,
                    readFunc, NULL, 0, "readtmp");
                return callResult;
            }
            return NULL;
        }
        default:
            return NULL;
    }
}

// genIRStmt: Generate LLVM IR for a statement node
static LLVMBasicBlockRef genIRStmt(astNode* node, LLVMBasicBlockRef startBB) {
    if (node == NULL) return startBB;
    if (node->type != ast_stmt) return startBB;

    LLVMValueRef currentFunc = LLVMGetBasicBlockParent(startBB);

    switch (node->stmt.type) {
        case ast_decl: {
            // Declarations are handled during alloca phase; nothing to do here
            return startBB;
        }
        case ast_asgn: {
            LLVMPositionBuilderAtEnd(builder, startBB);
            LLVMValueRef rhsVal = genIRExpr(node->stmt.asgn.rhs);
            string lhsName = node->stmt.asgn.lhs->var.name;
            LLVMValueRef lhsAlloca = var_map[lhsName];
            LLVMBuildStore(builder, rhsVal, lhsAlloca);
            return startBB;
        }
        case ast_call: {
            // print(expr)
            LLVMPositionBuilderAtEnd(builder, startBB);
            LLVMValueRef argVal = genIRExpr(node->stmt.call.param);
            LLVMValueRef args[] = { argVal };
            LLVMBuildCall2(builder, printFuncType, printFunc, args, 1, "");
            return startBB;
        }
        case ast_while: {
            LLVMPositionBuilderAtEnd(builder, startBB);

            // Create condition check block
            LLVMBasicBlockRef condBB = LLVMAppendBasicBlock(currentFunc, "whcond");
            LLVMBuildBr(builder, condBB);

            // Generate condition
            LLVMPositionBuilderAtEnd(builder, condBB);
            LLVMValueRef condVal = genIRExpr(node->stmt.whilen.cond);

            // Create true and false blocks
            LLVMBasicBlockRef trueBB = LLVMAppendBasicBlock(currentFunc, "whtrue");
            LLVMBasicBlockRef falseBB = LLVMAppendBasicBlock(currentFunc, "whfalse");
            LLVMBuildCondBr(builder, condVal, trueBB, falseBB);

            // Generate while body
            LLVMBasicBlockRef trueExitBB = genIRStmt(node->stmt.whilen.body, trueBB);

            // Branch back to condition
            LLVMPositionBuilderAtEnd(builder, trueExitBB);
            LLVMBuildBr(builder, condBB);

            return falseBB;
        }
        case ast_if: {
            LLVMPositionBuilderAtEnd(builder, startBB);
            LLVMValueRef condVal = genIRExpr(node->stmt.ifn.cond);

            LLVMBasicBlockRef trueBB = LLVMAppendBasicBlock(currentFunc, "iftrue");
            LLVMBasicBlockRef falseBB = LLVMAppendBasicBlock(currentFunc, "iffalse");
            LLVMBuildCondBr(builder, condVal, trueBB, falseBB);

            if (node->stmt.ifn.else_body == NULL) {
                // No else part
                LLVMBasicBlockRef ifExitBB = genIRStmt(node->stmt.ifn.if_body, trueBB);
                LLVMPositionBuilderAtEnd(builder, ifExitBB);
                LLVMBuildBr(builder, falseBB);
                return falseBB;
            } else {
                // Has else part
                LLVMBasicBlockRef ifExitBB = genIRStmt(node->stmt.ifn.if_body, trueBB);
                LLVMBasicBlockRef elseExitBB = genIRStmt(node->stmt.ifn.else_body, falseBB);

                LLVMBasicBlockRef endBB = LLVMAppendBasicBlock(currentFunc, "ifend");
                LLVMPositionBuilderAtEnd(builder, ifExitBB);
                LLVMBuildBr(builder, endBB);
                LLVMPositionBuilderAtEnd(builder, elseExitBB);
                LLVMBuildBr(builder, endBB);

                return endBB;
            }
        }
        case ast_ret: {
            LLVMPositionBuilderAtEnd(builder, startBB);
            LLVMValueRef retVal = genIRExpr(node->stmt.ret.expr);
            LLVMBuildStore(builder, retVal, ret_ref);
            LLVMBuildBr(builder, retBB);

            // Create a new unreachable block for any following statements
            LLVMBasicBlockRef endBB = LLVMAppendBasicBlock(currentFunc, "retcont");
            return endBB;
        }
        case ast_block: {
            LLVMBasicBlockRef prevBB = startBB;
            vector<astNode*>* stmts = node->stmt.block.stmt_list;
            for (size_t i = 0; i < stmts->size(); i++) {
                prevBB = genIRStmt((*stmts)[i], prevBB);
            }
            return prevBB;
        }
        default:
            return startBB;
    }
}

// generateIR: Main entry point for IR generation
LLVMModuleRef generateIR(astNode* root) {
    if (root == NULL || root->type != ast_prog) return NULL;

    // Create module
    LLVMModuleRef module = LLVMModuleCreateWithName("miniC");
    LLVMSetTarget(module, "x86_64-pc-linux-gnu");

    // Declare extern void print(int)
    LLVMTypeRef printParamTypes[] = { LLVMInt32Type() };
    printFuncType = LLVMFunctionType(LLVMVoidType(), printParamTypes, 1, 0);
    printFunc = LLVMAddFunction(module, "print", printFuncType);

    // Declare extern int read()
    readFuncType = LLVMFunctionType(LLVMInt32Type(), NULL, 0, 1);
    readFunc = LLVMAddFunction(module, "read", readFuncType);

    // Process the function
    astNode* funcNode = root->prog.func;
    if (funcNode == NULL || funcNode->type != ast_func) return module;

    // Determine function signature
    bool hasParam = (funcNode->func.param != NULL);
    LLVMTypeRef funcRetType = LLVMInt32Type();
    LLVMTypeRef paramTypes[] = { LLVMInt32Type() };
    unsigned paramCount = hasParam ? 1 : 0;

    LLVMTypeRef funcType = LLVMFunctionType(funcRetType, paramTypes, paramCount, 0);
    LLVMValueRef function = LLVMAddFunction(module, funcNode->func.name, funcType);

    // Create builder
    builder = LLVMCreateBuilder();

    // Create entry basic block
    LLVMBasicBlockRef entryBB = LLVMAppendBasicBlock(function, "entry");

    // Collect all parameter and local variable names
    set<string> allNames;
    if (hasParam) {
        allNames.insert(funcNode->func.param->var.name);
    }
    collectDeclNames(funcNode->func.body, allNames);

    // Position builder at end of entry block
    LLVMPositionBuilderAtEnd(builder, entryBB);

    // Initialize var_map: allocate space for each variable
    var_map.clear();
    for (set<string>::iterator it = allNames.begin(); it != allNames.end(); ++it) {
        LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, LLVMInt32Type(), it->c_str());
        LLVMSetAlignment(alloca_inst, 4);
        var_map[*it] = alloca_inst;
    }

    // Allocate space for return value
    ret_ref = LLVMBuildAlloca(builder, LLVMInt32Type(), "retval");
    LLVMSetAlignment(ret_ref, 4);

    // Store the function parameter into its alloca
    if (hasParam) {
        string paramName = funcNode->func.param->var.name;
        LLVMValueRef paramVal = LLVMGetParam(function, 0);
        LLVMBuildStore(builder, paramVal, var_map[paramName]);
    }

    // Create return basic block
    retBB = LLVMAppendBasicBlock(function, "return");
    LLVMPositionBuilderAtEnd(builder, retBB);
    LLVMValueRef loadRet = LLVMBuildLoad2(builder, LLVMInt32Type(), ret_ref, "retload");
    LLVMBuildRet(builder, loadRet);

    // Generate IR for the function body
    LLVMBasicBlockRef exitBB = genIRStmt(funcNode->func.body, entryBB);

    // If exitBB has no terminator, add branch to retBB
    LLVMValueRef term = LLVMGetBasicBlockTerminator(exitBB);
    if (term == NULL) {
        LLVMPositionBuilderAtEnd(builder, exitBB);
        LLVMBuildBr(builder, retBB);
    }

    // Remove unreachable basic blocks
    removeDeadBlocks(function);

    // Cleanup
    var_map.clear();
    LLVMDisposeBuilder(builder);
    builder = NULL;

    return module;
}
