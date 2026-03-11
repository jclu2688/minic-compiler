#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include <llvm-c/Core.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Types.h>
#include "backend.h"

#define prt(x) if(x) { printf("%s\n", x); }

// Register constants
#define REG_EBX 0
#define REG_ECX 1
#define REG_EDX 2
#define REG_SPILL -1

static const char *reg_names[] = {"%ebx", "%ecx", "%edx"};
static const int NUM_REGS = 3;

// Liveness and Register Allocation Data Structures

// Maps LLVMValueRef -> index in basic block (ignoring allocas)
static std::map<LLVMValueRef, int> inst_index;

// Maps LLVMValueRef -> (start, end) live range
static std::map<LLVMValueRef, std::pair<int,int>> live_range;

// Maps LLVMValueRef -> assigned physical register (0,1,2) or -1 for spill
static std::map<LLVMValueRef, int> reg_map;

// Maps LLVMValueRef -> stack offset from %ebp
static std::map<LLVMValueRef, int> offset_map;

// Maps LLVMBasicBlockRef -> label string
static std::map<LLVMBasicBlockRef, std::string> bb_labels;

// Helper: check if an instruction produces a value (has a LHS)
static bool producesValue(LLVMValueRef inst) {
    LLVMOpcode op = LLVMGetInstructionOpcode(inst);
    switch (op) {
        case LLVMStore:
        case LLVMBr:
        case LLVMRet:
            return false;
        case LLVMCall: {
            // A call produces a value only if its return type is not void
            LLVMTypeRef retTy = LLVMTypeOf(inst);
            return LLVMGetTypeKind(retTy) != LLVMVoidTypeKind;
        }
        default:
            return true;
    }
}

// compute_liveness: Compute inst_index and live_range for a BB
static void compute_liveness(LLVMBasicBlockRef bb) {
    inst_index.clear();
    live_range.clear();

    // First pass: assign indices to non-alloca instructions
    int idx = 0;
    for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
         inst != NULL;
         inst = LLVMGetNextInstruction(inst)) {
        if (LLVMGetInstructionOpcode(inst) == LLVMAlloca)
            continue;
        inst_index[inst] = idx;
        idx++;
    }

    // Second pass: compute live ranges
    // For each instruction that produces a value, set start = its index
    // Then scan all instructions for uses to find the last use index
    for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
         inst != NULL;
         inst = LLVMGetNextInstruction(inst)) {
        if (LLVMGetInstructionOpcode(inst) == LLVMAlloca)
            continue;

        if (producesValue(inst)) {
            int defIdx = inst_index[inst];
            live_range[inst] = std::make_pair(defIdx, defIdx);
        }
    }

    // Update end points based on uses
    for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
         inst != NULL;
         inst = LLVMGetNextInstruction(inst)) {
        if (LLVMGetInstructionOpcode(inst) == LLVMAlloca)
            continue;

        int useIdx = inst_index[inst];
        int numOps = LLVMGetNumOperands(inst);
        for (int i = 0; i < numOps; i++) {
            LLVMValueRef operand = LLVMGetOperand(inst, i);
            // Only update if the operand was defined in this BB
            if (live_range.find(operand) != live_range.end()) {
                if (useIdx > live_range[operand].second) {
                    live_range[operand].second = useIdx;
                }
            }
        }
    }
}

// find_spill: Find the best spill candidate
// Heuristic: spill the value with the longest remaining live range
static LLVMValueRef find_spill(LLVMValueRef instr) {
    LLVMValueRef best = NULL;
    int bestEnd = -1;

    // Look through all values that have a register assigned
    for (auto &entry : reg_map) {
        LLVMValueRef v = entry.first;
        int reg = entry.second;
        if (reg == REG_SPILL) continue; // already spilled

        // Check if v has overlapping liveness with instr
        if (live_range.find(v) == live_range.end()) continue;
        if (live_range.find(instr) == live_range.end()) continue;

        int vStart = live_range[v].first;
        int vEnd = live_range[v].second;
        int instrStart = live_range[instr].first;
        int instrEnd = live_range[instr].second;

        // Check overlap: ranges overlap if one starts before the other ends
        if (vStart <= instrEnd && instrStart <= vEnd) {
            // Overlapping — candidate for spill
            if (vEnd > bestEnd) {
                bestEnd = vEnd;
                best = v;
            }
        }
    }

    return best;
}

// linear_scan_reg_alloc: Register allocation for all BBs
static void linear_scan_reg_alloc(LLVMValueRef function) {
    reg_map.clear();

    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {

        // Initialize available registers
        std::set<int> available;
        available.insert(REG_EBX);
        available.insert(REG_ECX);
        available.insert(REG_EDX);

        // Compute liveness for this BB
        compute_liveness(bb);

        for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {

            LLVMOpcode op = LLVMGetInstructionOpcode(inst);

            // Ignore alloca instructions
            if (op == LLVMAlloca) continue;

            // If instruction does not produce a value (store, branch, void call)
            if (!producesValue(inst)) {
                // Free registers of operands whose live range ends here
                int numOps = LLVMGetNumOperands(inst);
                for (int i = 0; i < numOps; i++) {
                    LLVMValueRef operand = LLVMGetOperand(inst, i);
                    if (live_range.find(operand) != live_range.end() &&
                        inst_index.find(inst) != inst_index.end()) {
                        if (live_range[operand].second == inst_index[inst]) {
                            if (reg_map.find(operand) != reg_map.end() &&
                                reg_map[operand] != REG_SPILL) {
                                available.insert(reg_map[operand]);
                            }
                        }
                    }
                }
                continue;
            }

            // Instruction produces a value — assign a register
            // Special case: add/sub/mul where first operand has a register
            // and its live range ends here — reuse that register
            if (op == LLVMAdd || op == LLVMSub || op == LLVMMul) {
                LLVMValueRef firstOp = LLVMGetOperand(inst, 0);
                if (reg_map.find(firstOp) != reg_map.end() &&
                    reg_map[firstOp] != REG_SPILL &&
                    live_range.find(firstOp) != live_range.end() &&
                    live_range[firstOp].second == inst_index[inst]) {
                    int R = reg_map[firstOp];
                    reg_map[inst] = R;

                    // Free second operand's register if its range ends here
                    LLVMValueRef secondOp = LLVMGetOperand(inst, 1);
                    if (live_range.find(secondOp) != live_range.end() &&
                        live_range[secondOp].second == inst_index[inst]) {
                        if (reg_map.find(secondOp) != reg_map.end() &&
                            reg_map[secondOp] != REG_SPILL) {
                            available.insert(reg_map[secondOp]);
                        }
                    }
                    continue;
                }
            }

            // Try to assign an available register
            if (!available.empty()) {
                int R = *available.begin();
                available.erase(available.begin());
                reg_map[inst] = R;

                // Free registers of operands whose live range ends here
                int numOps = LLVMGetNumOperands(inst);
                for (int i = 0; i < numOps; i++) {
                    LLVMValueRef operand = LLVMGetOperand(inst, i);
                    if (live_range.find(operand) != live_range.end() &&
                        live_range[operand].second == inst_index[inst]) {
                        if (reg_map.find(operand) != reg_map.end() &&
                            reg_map[operand] != REG_SPILL) {
                            available.insert(reg_map[operand]);
                        }
                    }
                }
            } else {
                // No register available — must spill
                LLVMValueRef V = find_spill(inst);
                if (V != NULL) {
                    // Compare: spill V if instr has a shorter (or equal) live range
                    int vEnd = live_range[V].second;
                    int instrEnd = live_range[inst].second;
                    if (instrEnd >= vEnd) {
                        // Spill the current instruction (V has fewer remaining uses)
                        reg_map[inst] = REG_SPILL;
                    } else {
                        // Spill V, give its register to inst
                        int R = reg_map[V];
                        reg_map[inst] = R;
                        reg_map[V] = REG_SPILL;
                    }
                } else {
                    reg_map[inst] = REG_SPILL;
                }

                // Free registers of operands whose live range ends here
                int numOps = LLVMGetNumOperands(inst);
                for (int i = 0; i < numOps; i++) {
                    LLVMValueRef operand = LLVMGetOperand(inst, i);
                    if (live_range.find(operand) != live_range.end() &&
                        live_range[operand].second == inst_index[inst]) {
                        if (reg_map.find(operand) != reg_map.end() &&
                            reg_map[operand] != REG_SPILL) {
                            available.insert(reg_map[operand]);
                        }
                    }
                }
            }
        }
    }
}

// createBBLabels: Assign labels to basic blocks
static void createBBLabels(LLVMValueRef function) {
    bb_labels.clear();
    int idx = 0;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        char label[64];
        snprintf(label, sizeof(label), ".BB_%d", idx);
        bb_labels[bb] = label;
        idx++;
    }
}

// printDirectives: Emit assembly directives
static void printDirectives(FILE *out, const char *funcName) {
    fprintf(out, "\t.section\t.note.GNU-stack,\"\",@progbits\n");
    fprintf(out, "\t.text\n");
    fprintf(out, "\t.globl\t%s\n", funcName);
    fprintf(out, "\t.type\t%s, @function\n", funcName);
    fprintf(out, "%s:\n", funcName);
}

// printFunctionEnd: Emit function epilogue
static void printFunctionEnd(FILE *out) {
    fprintf(out, "\tleave\n");
    fprintf(out, "\tret\n");
}

// getOffsetMap: Compute stack offsets for all values
static int getOffsetMap(LLVMValueRef function) {
    offset_map.clear();
    int localMem = 4;

    // If the function has a parameter, map it to offset +8 (above saved %ebp and return addr)
    unsigned numParams = LLVMCountParams(function);
    LLVMValueRef param = NULL;
    if (numParams > 0) {
        param = LLVMGetParam(function, 0);
        offset_map[param] = 8;
    }

    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {

            LLVMOpcode op = LLVMGetInstructionOpcode(inst);

            if (op == LLVMAlloca) {
                localMem += 4;
                offset_map[inst] = -1 * localMem;
            }
            else if (op == LLVMStore) {
                LLVMValueRef storeVal = LLVMGetOperand(inst, 0);  // value being stored
                LLVMValueRef storeDst = LLVMGetOperand(inst, 1);  // destination

                if (param != NULL && storeVal == param) {
                    // Store of parameter: map destination to parameter's offset
                    if (offset_map.find(storeVal) != offset_map.end()) {
                        int x = offset_map[storeVal];
                        offset_map[storeDst] = x;
                    }
                } else if (!LLVMIsConstant(storeVal)) {
                    // Store of a temporary: map the temporary to destination's offset
                    if (offset_map.find(storeDst) != offset_map.end()) {
                        int x = offset_map[storeDst];
                        offset_map[storeVal] = x;
                    }
                }
            }
            else if (op == LLVMLoad) {
                LLVMValueRef loadSrc = LLVMGetOperand(inst, 0);  // source pointer
                if (offset_map.find(loadSrc) != offset_map.end()) {
                    int x = offset_map[loadSrc];
                    offset_map[inst] = x;
                }
            }
        }
    }

    return localMem;
}

// Helper: get register name or "%eax" for spilled/unassigned values
static const char *getRegName(LLVMValueRef val) {
    if (reg_map.find(val) != reg_map.end() && reg_map[val] != REG_SPILL) {
        return reg_names[reg_map[val]];
    }
    return "%eax";
}

static bool hasPhysReg(LLVMValueRef val) {
    return reg_map.find(val) != reg_map.end() && reg_map[val] != REG_SPILL;
}

static bool isInMemory(LLVMValueRef val) {
    return !hasPhysReg(val);
}

static int getOffset(LLVMValueRef val) {
    if (offset_map.find(val) != offset_map.end()) {
        return offset_map[val];
    }
    return 0; // fallback
}

// Helper: get the opcode string for arithmetic instructions
static const char *getArithOp(LLVMOpcode op) {
    switch (op) {
        case LLVMAdd: return "addl";
        case LLVMSub: return "subl";
        case LLVMMul: return "imull";
        default: return "addl";
    }
}

// Helper: get the jump instruction for an icmp predicate
static const char *getCondJump(LLVMIntPredicate pred) {
    switch (pred) {
        case LLVMIntEQ:  return "je";
        case LLVMIntNE:  return "jne";
        case LLVMIntSGT: return "jg";
        case LLVMIntSGE: return "jge";
        case LLVMIntSLT: return "jl";
        case LLVMIntSLE: return "jle";
        case LLVMIntUGT: return "ja";
        case LLVMIntUGE: return "jae";
        case LLVMIntULT: return "jb";
        case LLVMIntULE: return "jbe";
        default:         return "je";
    }
}

// generateAssemblyForFunction: Main code generation loop
static void generateAssemblyForFunction(FILE *out, LLVMValueRef function) {
    // Get function name
    size_t nameLen;
    const char *funcName = LLVMGetValueName2(function, &nameLen);

    // Step 1: Create BB labels
    createBBLabels(function);

    // Step 2: Print directives
    printDirectives(out, funcName);

    // Step 3: Compute offset map and localMem
    int localMem = getOffsetMap(function);

    // Step 4: Register allocation
    linear_scan_reg_alloc(function);

    // Step 5: Function prologue
    fprintf(out, "\tpushl\t%%ebp\n");
    fprintf(out, "\tmovl\t%%esp, %%ebp\n");
    fprintf(out, "\tsubl\t$%d, %%esp\n", localMem);
    fprintf(out, "\tpushl\t%%ebx\n");

    // Step 6: Emit code for each basic block
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {

        // Print basic block label
        fprintf(out, "%s:\n", bb_labels[bb].c_str());

        for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {

            LLVMOpcode op = LLVMGetInstructionOpcode(inst);

            // RETURN: ret i32 A
            if (op == LLVMRet) {
                if (LLVMGetNumOperands(inst) > 0) {
                    LLVMValueRef A = LLVMGetOperand(inst, 0);
                    if (LLVMIsConstant(A)) {
                        long long val = LLVMConstIntGetSExtValue(A);
                        fprintf(out, "\tmovl\t$%lld, %%eax\n", val);
                    } else if (hasPhysReg(A)) {
                        fprintf(out, "\tmovl\t%s, %%eax\n", getRegName(A));
                    } else {
                        int k = getOffset(A);
                        fprintf(out, "\tmovl\t%d(%%ebp), %%eax\n", k);
                    }
                }
                fprintf(out, "\tpopl\t%%ebx\n");
                printFunctionEnd(out);
            }
            // LOAD: %a = load i32, i32* %b
            else if (op == LLVMLoad) {
                if (hasPhysReg(inst)) {
                    LLVMValueRef src = LLVMGetOperand(inst, 0);
                    int c = getOffset(src);
                    fprintf(out, "\tmovl\t%d(%%ebp), %s\n", c, getRegName(inst));
                }
                // If spilled, no load instruction needed — accessed from memory directly
            }
            // STORE: store i32 A, i32* %b
            else if (op == LLVMStore) {
                LLVMValueRef A = LLVMGetOperand(inst, 0);
                LLVMValueRef b = LLVMGetOperand(inst, 1);

                // Check if A is function parameter — if so, ignore
                unsigned numParams = LLVMCountParams(function);
                LLVMValueRef param = (numParams > 0) ? LLVMGetParam(function, 0) : NULL;
                if (param != NULL && A == param) {
                    continue; // ignore parameter store
                }

                if (LLVMIsConstant(A)) {
                    long long val = LLVMConstIntGetSExtValue(A);
                    int c = getOffset(b);
                    fprintf(out, "\tmovl\t$%lld, %d(%%ebp)\n", val, c);
                } else if (hasPhysReg(A)) {
                    int c = getOffset(b);
                    fprintf(out, "\tmovl\t%s, %d(%%ebp)\n", getRegName(A), c);
                } else {
                    // A is in memory
                    int c1 = getOffset(A);
                    int c2 = getOffset(b);
                    fprintf(out, "\tmovl\t%d(%%ebp), %%eax\n", c1);
                    fprintf(out, "\tmovl\t%%eax, %d(%%ebp)\n", c2);
                }
            }
            // CALL: %a = call type @func() or call type @func(P)
            else if (op == LLVMCall) {
                // Push caller-saved registers
                fprintf(out, "\tpushl\t%%ecx\n");
                fprintf(out, "\tpushl\t%%edx\n");

                // Get the called function
                LLVMValueRef calledFunc = LLVMGetCalledValue(inst);
                size_t calledNameLen;
                const char *calledName = LLVMGetValueName2(calledFunc, &calledNameLen);

                // Check for parameter
                unsigned numArgs = LLVMGetNumArgOperands(inst);
                if (numArgs > 0) {
                    LLVMValueRef P = LLVMGetOperand(inst, 0);
                    if (LLVMIsConstant(P)) {
                        long long val = LLVMConstIntGetSExtValue(P);
                        fprintf(out, "\tpushl\t$%lld\n", val);
                    } else if (hasPhysReg(P)) {
                        fprintf(out, "\tpushl\t%s\n", getRegName(P));
                    } else {
                        int k = getOffset(P);
                        fprintf(out, "\tpushl\t%d(%%ebp)\n", k);
                    }
                }

                // Emit call
                fprintf(out, "\tcall\t%s\n", calledName);

                // Undo parameter push
                if (numArgs > 0) {
                    fprintf(out, "\taddl\t$4, %%esp\n");
                }

                // Restore caller-saved registers
                fprintf(out, "\tpopl\t%%edx\n");
                fprintf(out, "\tpopl\t%%ecx\n");

                // If call returns a value, move %eax to the assigned register
                LLVMTypeRef retTy = LLVMTypeOf(inst);
                if (LLVMGetTypeKind(retTy) != LLVMVoidTypeKind) {
                    if (hasPhysReg(inst)) {
                        fprintf(out, "\tmovl\t%%eax, %s\n", getRegName(inst));
                    } else if (isInMemory(inst)) {
                        int k = getOffset(inst);
                        fprintf(out, "\tmovl\t%%eax, %d(%%ebp)\n", k);
                    }
                }
            }
            // BRANCH: br label %b  or  br i1 %a, label %b, label %c
            else if (op == LLVMBr) {
                if (LLVMIsConditional(inst)) {
                    // Conditional branch: br i1 %a, label %b, label %c
                    // Note: %b is operand 2, %c is operand 1 (per task.md)
                    LLVMValueRef cond = LLVMGetCondition(inst);
                    LLVMBasicBlockRef trueBB = LLVMGetSuccessor(inst, 0);   // %b (true target)
                    LLVMBasicBlockRef falseBB = LLVMGetSuccessor(inst, 1);  // %c (false target)

                    // Get the comparison predicate from the condition instruction
                    LLVMIntPredicate pred = LLVMGetICmpPredicate(cond);
                    const char *jmpInstr = getCondJump(pred);

                    std::string trueLabel = bb_labels[trueBB];
                    std::string falseLabel = bb_labels[falseBB];

                    fprintf(out, "\t%s\t%s\n", jmpInstr, trueLabel.c_str());
                    fprintf(out, "\tjmp\t%s\n", falseLabel.c_str());
                } else {
                    // Unconditional branch: br label %b
                    LLVMBasicBlockRef targetBB = LLVMGetSuccessor(inst, 0);
                    std::string label = bb_labels[targetBB];
                    fprintf(out, "\tjmp\t%s\n", label.c_str());
                }
            }
            // ARITHMETIC: %a = add/sub/mul nsw A, B
            else if (op == LLVMAdd || op == LLVMSub || op == LLVMMul) {
                LLVMValueRef A = LLVMGetOperand(inst, 0);
                LLVMValueRef B = LLVMGetOperand(inst, 1);
                const char *arithOp = getArithOp(op);

                // Determine R: the register for %a
                const char *R = hasPhysReg(inst) ? getRegName(inst) : "%eax";

                // Emit: move A into R
                if (LLVMIsConstant(A)) {
                    long long val = LLVMConstIntGetSExtValue(A);
                    fprintf(out, "\tmovl\t$%lld, %s\n", val, R);
                } else if (hasPhysReg(A)) {
                    // Don't emit if both registers are the same
                    if (strcmp(getRegName(A), R) != 0) {
                        fprintf(out, "\tmovl\t%s, %s\n", getRegName(A), R);
                    }
                } else {
                    int n = getOffset(A);
                    fprintf(out, "\tmovl\t%d(%%ebp), %s\n", n, R);
                }

                // Emit: arithOp B, R
                if (LLVMIsConstant(B)) {
                    long long val = LLVMConstIntGetSExtValue(B);
                    fprintf(out, "\t%s\t$%lld, %s\n", arithOp, val, R);
                } else if (hasPhysReg(B)) {
                    fprintf(out, "\t%s\t%s, %s\n", arithOp, getRegName(B), R);
                } else {
                    int m = getOffset(B);
                    fprintf(out, "\t%s\t%d(%%ebp), %s\n", arithOp, m, R);
                }

                // If %a is in memory, store result from %eax
                if (!hasPhysReg(inst)) {
                    int k = getOffset(inst);
                    fprintf(out, "\tmovl\t%%eax, %d(%%ebp)\n", k);
                }
            }
            // COMPARE: %a = icmp slt A, B
            else if (op == LLVMICmp) {
                LLVMValueRef A = LLVMGetOperand(inst, 0);
                LLVMValueRef B = LLVMGetOperand(inst, 1);

                // Determine R: the register for %a
                const char *R = hasPhysReg(inst) ? getRegName(inst) : "%eax";

                // Emit: move A into R
                if (LLVMIsConstant(A)) {
                    long long val = LLVMConstIntGetSExtValue(A);
                    fprintf(out, "\tmovl\t$%lld, %s\n", val, R);
                } else if (hasPhysReg(A)) {
                    if (strcmp(getRegName(A), R) != 0) {
                        fprintf(out, "\tmovl\t%s, %s\n", getRegName(A), R);
                    }
                } else {
                    int n = getOffset(A);
                    fprintf(out, "\tmovl\t%d(%%ebp), %s\n", n, R);
                }

                // Emit: cmpl B, R
                if (LLVMIsConstant(B)) {
                    long long val = LLVMConstIntGetSExtValue(B);
                    fprintf(out, "\tcmpl\t$%lld, %s\n", val, R);
                } else if (hasPhysReg(B)) {
                    fprintf(out, "\tcmpl\t%s, %s\n", getRegName(B), R);
                } else {
                    int m = getOffset(B);
                    fprintf(out, "\tcmpl\t%d(%%ebp), %s\n", m, R);
                }
            }
            // Alloca instructions — skip
            else if (op == LLVMAlloca) {
                // do nothing
            }
        }
    }
}

// generateAssembly: Top-level entry point
void generateAssembly(LLVMModuleRef mod, const char *outputFile) {
    FILE *out = fopen(outputFile, "w");
    if (!out) {
        fprintf(stderr, "Error: cannot open output file '%s'\n", outputFile);
        return;
    }

    for (LLVMValueRef function = LLVMGetFirstFunction(mod);
         function != NULL;
         function = LLVMGetNextFunction(function)) {
        // Skip declarations (functions with no body)
        if (LLVMCountBasicBlocks(function) == 0) continue;

        generateAssemblyForFunction(out, function);
    }

    fclose(out);
}

// Standalone mode
#ifdef STANDALONE_BACKEND
static LLVMModuleRef createLLVMModel(const char *filename) {
    char *err = 0;
    LLVMMemoryBufferRef ll_f = 0;
    LLVMModuleRef m = 0;

    LLVMCreateMemoryBufferWithContentsOfFile(filename, &ll_f, &err);
    if (err != NULL) {
        prt(err);
        return NULL;
    }

    LLVMParseIRInContext(LLVMGetGlobalContext(), ll_f, &m, &err);
    if (err != NULL) {
        prt(err);
    }

    return m;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <input.ll> [output.s]\n", argv[0]);
        return 1;
    }

    LLVMModuleRef m = createLLVMModel(argv[1]);
    if (m == NULL) {
        fprintf(stderr, "Error: could not parse LLVM IR file '%s'\n", argv[1]);
        return 1;
    }

    const char *outfile = (argc == 3) ? argv[2] : "output.s";
    generateAssembly(m, outfile);

    printf("Assembly written to %s\n", outfile);
    LLVMDisposeModule(m);
    return 0;
}
#endif
