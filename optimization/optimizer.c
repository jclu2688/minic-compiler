#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Types.h>
#include "optimizer.h"

#define prt(x) if(x) { printf("%s\n", x); }

// Set data structure for LLVMValueRef (used by dataflow analysis)

typedef struct {
    LLVMValueRef *items;
    int size;
    int capacity;
} ValueSet;

void setInit(ValueSet *s) {
    s->items = NULL;
    s->size = 0;
    s->capacity = 0;
}

void setFree(ValueSet *s) {
    free(s->items);
    s->items = NULL;
    s->size = 0;
    s->capacity = 0;
}


// Set operation helper functions:
bool setContains(ValueSet *s, LLVMValueRef val) {
    for (int i = 0; i < s->size; i++) {
        if (s->items[i] == val) return true;
    }
    return false;
}

void setAdd(ValueSet *s, LLVMValueRef val) {
    if (setContains(s, val)) return;
    if (s->size >= s->capacity) {
        s->capacity = (s->capacity == 0) ? 8 : s->capacity * 2;
        s->items = (LLVMValueRef *)realloc(s->items, s->capacity * sizeof(LLVMValueRef));
    }
    s->items[s->size++] = val;
}

void setRemove(ValueSet *s, LLVMValueRef val) {
    for (int i = 0; i < s->size; i++) {
        if (s->items[i] == val) {
            s->items[i] = s->items[s->size - 1];
            s->size--;
            return;
        }
    }
}

void setCopy(ValueSet *dst, ValueSet *src) {
    dst->size = 0;
    for (int i = 0; i < src->size; i++) {
        setAdd(dst, src->items[i]);
    }
}

// a U b
void setUnion(ValueSet *dst, ValueSet *a, ValueSet *b) {
    dst->size = 0;
    for (int i = 0; i < a->size; i++) {
        setAdd(dst, a->items[i]);
    }
    for (int i = 0; i < b->size; i++) {
        setAdd(dst, b->items[i]);
    }
}

// a - b (set difference)
void setDifference(ValueSet *dst, ValueSet *a, ValueSet *b) {
    dst->size = 0;
    for (int i = 0; i < a->size; i++) {
        if (!setContains(b, a->items[i])) {
            setAdd(dst, a->items[i]);
        }
    }
}

// a U (b - c)
void setUnionDiff(ValueSet *dst, ValueSet *a, ValueSet *b, ValueSet *c) {
    ValueSet temp;
    setInit(&temp);
    setDifference(&temp, b, c);
    setUnion(dst, a, &temp);
    setFree(&temp);
}

bool setEqual(ValueSet *a, ValueSet *b) {
    if (a->size != b->size) return false;
    for (int i = 0; i < a->size; i++) {
        if (!setContains(b, a->items[i])) return false;
    }
    return true;
}


// copied from llvm_parser.c from in-class activity
LLVMModuleRef createLLVMModel(char *filename) {
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

// check if two instructions have same opcode and operands
static bool sameOpcodeAndOperands(LLVMValueRef a, LLVMValueRef b) {
    if (LLVMGetInstructionOpcode(a) != LLVMGetInstructionOpcode(b))
        return false;

    int numOps = LLVMGetNumOperands(a);
    if (numOps != LLVMGetNumOperands(b))
        return false;

    for (int i = 0; i < numOps; i++) {
        if (LLVMGetOperand(a, i) != LLVMGetOperand(b, i))
            return false;
    }
    return true;
}

// check if a store instruction exists between loadA and loadB that writes to the same address as loadA
static bool hasInterveningStore(LLVMValueRef loadA, LLVMValueRef loadB) {
    // loadA's operand 0 is the address being loaded from
    LLVMValueRef addr = LLVMGetOperand(loadA, 0);

    // Walk from the instruction after A to B
    LLVMValueRef inst = LLVMGetNextInstruction(loadA);
    while (inst != NULL && inst != loadB) {
        if (LLVMGetInstructionOpcode(inst) == LLVMStore) {
            // Store instruction: operand 0 = value, operand 1 = address
            LLVMValueRef storeAddr = LLVMGetOperand(inst, 1);
            if (storeAddr == addr) {
                return true;
            }
        }
        inst = LLVMGetNextInstruction(inst);
    }
    return false;
}

// LOCAL OPTIMIZATION 1: Common Subexpression Elimination
void commonSubExprElim(LLVMBasicBlockRef bb) {
    for (LLVMValueRef instA = LLVMGetFirstInstruction(bb);
         instA != NULL;
         instA = LLVMGetNextInstruction(instA)) {

        LLVMOpcode opA = LLVMGetInstructionOpcode(instA);

        // Skip call, store, terminator, and alloca instructions
        if (opA == LLVMCall || opA == LLVMStore || opA == LLVMAlloca)
            continue;
        if (LLVMIsATerminatorInst(instA))
            continue;

        for (LLVMValueRef instB = LLVMGetNextInstruction(instA);
             instB != NULL; ) {

            LLVMValueRef nextB = LLVMGetNextInstruction(instB);

            LLVMOpcode opB = LLVMGetInstructionOpcode(instB);
            if (opB == LLVMCall || opB == LLVMStore || opB == LLVMAlloca) {
                instB = nextB;
                continue;
            }
            if (LLVMIsATerminatorInst(instB)) {
                instB = nextB;
                continue;
            }

            if (sameOpcodeAndOperands(instA, instB)) {
                // Safety check for load instructions
                if (opA == LLVMLoad) {
                    if (hasInterveningStore(instA, instB)) {
                        instB = nextB;
                        continue;
                    }
                }
                // Replace all uses of B with A
                LLVMReplaceAllUsesWith(instB, instA);
            }

            instB = nextB;
        }
    }
}

// LOCAL OPTIMIZATION 2: Dead Code Elimination
bool deadCodeElim(LLVMValueRef function) {
    bool changed = false;
    bool localChanged = true;

    while (localChanged) {
        localChanged = false;
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
             bb != NULL;
             bb = LLVMGetNextBasicBlock(bb)) {

            LLVMValueRef inst = LLVMGetFirstInstruction(bb);
            while (inst != NULL) {
                LLVMValueRef next = LLVMGetNextInstruction(inst);
                LLVMOpcode op = LLVMGetInstructionOpcode(inst);

                // Don't delete instructions with side effects
                if (op == LLVMStore || op == LLVMCall || op == LLVMAlloca)
                    goto skip;
                if (LLVMIsATerminatorInst(inst))
                    goto skip;
                if (op == LLVMPHI)
                    goto skip;

                // Delete if no uses
                if (LLVMGetFirstUse(inst) == NULL) {
                    LLVMInstructionEraseFromParent(inst);
                    localChanged = true;
                    changed = true;
                }

                skip:
                inst = next;
            }
        }
    }
    return changed;
}

// LOCAL OPTIMIZATION 3: Constant Folding
bool constantFolding(LLVMValueRef function) {
    bool changed = false;

    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {

        LLVMValueRef inst = LLVMGetFirstInstruction(bb);
        while (inst != NULL) {
            LLVMValueRef next = LLVMGetNextInstruction(inst);
            LLVMOpcode op = LLVMGetInstructionOpcode(inst);

            if (op == LLVMAdd || op == LLVMSub || op == LLVMMul) {
                LLVMValueRef op0 = LLVMGetOperand(inst, 0);
                LLVMValueRef op1 = LLVMGetOperand(inst, 1);

                if (LLVMIsConstant(op0) && LLVMIsConstant(op1)) {
                    LLVMValueRef result = NULL;

                    if (op == LLVMAdd) {
                        result = LLVMConstAdd(op0, op1);
                    } else if (op == LLVMSub) {
                        result = LLVMConstSub(op0, op1);
                    } else if (op == LLVMMul) {
                        result = LLVMConstMul(op0, op1);
                    }

                    if (result != NULL) {
                        LLVMReplaceAllUsesWith(inst, result);
                        changed = true;
                    }
                }
            }

            inst = next;
        }
    }
    return changed;
}

// GLOBAL OPTIMIZATION: Constant Propagation

// check if a store instruction kills another store instruction
static bool storeKills(LLVMValueRef killer, LLVMValueRef victim) {
    LLVMValueRef addr1 = LLVMGetOperand(killer, 1);
    LLVMValueRef addr2 = LLVMGetOperand(victim, 1);
    return (addr1 == addr2);
}

// check if a store instruction is a "constant store"
static bool isConstantStore(LLVMValueRef store) {
    LLVMValueRef val = LLVMGetOperand(store, 0);
    return LLVMIsConstant(val) && LLVMIsAConstantInt(val);
}

bool constantPropagation(LLVMValueRef function) {
    bool changed = false;

    // collect all store instructions in the function (set S)
    ValueSet allStores;
    setInit(&allStores);

    // count basic blocks
    int numBBs = 0;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        numBBs++;
    }

    if (numBBs == 0) {
        setFree(&allStores);
        return false;
    }

    // create arrays for basic blocks and their sets
    LLVMBasicBlockRef *bbs = (LLVMBasicBlockRef *)malloc(numBBs * sizeof(LLVMBasicBlockRef));
    ValueSet *gen  = (ValueSet *)malloc(numBBs * sizeof(ValueSet));
    ValueSet *kill = (ValueSet *)malloc(numBBs * sizeof(ValueSet));
    ValueSet *in   = (ValueSet *)malloc(numBBs * sizeof(ValueSet));
    ValueSet *out  = (ValueSet *)malloc(numBBs * sizeof(ValueSet));

    int idx = 0;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        bbs[idx] = bb;
        setInit(&gen[idx]);
        setInit(&kill[idx]);
        setInit(&in[idx]);
        setInit(&out[idx]);
        idx++;

        // collect all stores
        for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {
            if (LLVMGetInstructionOpcode(inst) == LLVMStore) {
                setAdd(&allStores, inst);
            }
        }
    }

    // compute GEN and KILL for each basic block
    for (int i = 0; i < numBBs; i++) {
        // compute GEN[i]
        for (LLVMValueRef inst = LLVMGetFirstInstruction(bbs[i]);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {

            if (LLVMGetInstructionOpcode(inst) == LLVMStore) {
                // Add this store to GEN
                setAdd(&gen[i], inst);

                // Remove any stores in GEN that are killed by this store
                for (int j = gen[i].size - 1; j >= 0; j--) {
                    if (gen[i].items[j] != inst &&
                        storeKills(inst, gen[i].items[j])) {
                        setRemove(&gen[i], gen[i].items[j]);
                    }
                }
            }
        }

        // compute KILL[i]
        for (LLVMValueRef inst = LLVMGetFirstInstruction(bbs[i]);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {

            if (LLVMGetInstructionOpcode(inst) == LLVMStore) {
                // add all stores in S that are killed by inst (but not inst itself)
                for (int j = 0; j < allStores.size; j++) {
                    if (allStores.items[j] != inst &&
                        storeKills(inst, allStores.items[j])) {
                        setAdd(&kill[i], allStores.items[j]);
                    }
                }
            }
        }
    }

    // iterative computation of IN and OUT sets
    // initialize: IN[B] = empty, OUT[B] = GEN[B]
    for (int i = 0; i < numBBs; i++) {
        setCopy(&out[i], &gen[i]);
    }

    bool fixpoint = false;
    while (!fixpoint) {
        fixpoint = true;
        for (int i = 0; i < numBBs; i++) {
            // IN[B] = union of OUT[P] for all predecessors P of B
            ValueSet newIn;
            setInit(&newIn);

            // find predecessors - iterate all BBs and check if they have B as a successor
            for (int j = 0; j < numBBs; j++) {
                LLVMValueRef term = LLVMGetBasicBlockTerminator(bbs[j]);
                if (term == NULL) continue;
                unsigned numSucc = LLVMGetNumSuccessors(term);
                for (unsigned s = 0; s < numSucc; s++) {
                    if (LLVMGetSuccessor(term, s) == bbs[i]) {
                        // bbs[j] is a predecessor of bbs[i]
                        for (int k = 0; k < out[j].size; k++) {
                            setAdd(&newIn, out[j].items[k]);
                        }
                        break;
                    }
                }
            }
            setCopy(&in[i], &newIn);
            setFree(&newIn);

            // OUT[B] = GEN[B] union (IN[B] - KILL[B])
            ValueSet oldOut;
            setInit(&oldOut);
            setCopy(&oldOut, &out[i]);

            ValueSet newOut;
            setInit(&newOut);
            setUnionDiff(&newOut, &gen[i], &in[i], &kill[i]);
            setCopy(&out[i], &newOut);

            if (!setEqual(&oldOut, &out[i])) {
                fixpoint = false;
            }

            setFree(&oldOut);
            setFree(&newOut);
        }
    }

    // walk each BB and check loads
    ValueSet toDelete;
    setInit(&toDelete);

    for (int i = 0; i < numBBs; i++) {
        ValueSet R;
        setInit(&R);
        setCopy(&R, &in[i]);

        for (LLVMValueRef inst = LLVMGetFirstInstruction(bbs[i]);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {

            if (LLVMGetInstructionOpcode(inst) == LLVMStore) {
                // add this store to R
                setAdd(&R, inst);

                // remove stores in R that are killed by this store
                for (int j = R.size - 1; j >= 0; j--) {
                    if (R.items[j] != inst && storeKills(inst, R.items[j])) {
                        setRemove(&R, R.items[j]);
                    }
                }
            }
            else if (LLVMGetInstructionOpcode(inst) == LLVMLoad) {
                // load from address (operand 0)
                LLVMValueRef loadAddr = LLVMGetOperand(inst, 0);

                // find all stores in R that write to the same address
                ValueSet reachingStores;
                setInit(&reachingStores);

                for (int j = 0; j < R.size; j++) {
                    LLVMValueRef storeAddr = LLVMGetOperand(R.items[j], 1);
                    if (storeAddr == loadAddr) {
                        setAdd(&reachingStores, R.items[j]);
                    }
                }

                // check if all reaching stores are constant stores with the same constant value
                if (reachingStores.size > 0) {
                    bool allConstant = true;
                    long long constVal = 0;
                    bool first = true;
                    bool allSame = true;

                    for (int j = 0; j < reachingStores.size; j++) {
                        if (!isConstantStore(reachingStores.items[j])) {
                            allConstant = false;
                            break;
                        }
                        LLVMValueRef storeVal = LLVMGetOperand(reachingStores.items[j], 0);
                        long long val = LLVMConstIntGetSExtValue(storeVal);
                        if (first) {
                            constVal = val;
                            first = false;
                        } else if (val != constVal) {
                            allSame = false;
                            break;
                        }
                    }

                    if (allConstant && allSame) {
                        // replace all uses of load with the constant
                        LLVMTypeRef loadType = LLVMTypeOf(inst);
                        LLVMValueRef constInt = LLVMConstInt(loadType,
                                                             (unsigned long long)constVal, 0);
                        LLVMReplaceAllUsesWith(inst, constInt);
                        setAdd(&toDelete, inst);
                        changed = true;
                    }
                }

                setFree(&reachingStores);
            }
        }

        setFree(&R);
    }

    // delete marked load instructions
    for (int i = 0; i < toDelete.size; i++) {
        LLVMInstructionEraseFromParent(toDelete.items[i]);
    }

    // cleanup
    setFree(&toDelete);
    setFree(&allStores);
    for (int i = 0; i < numBBs; i++) {
        setFree(&gen[i]);
        setFree(&kill[i]);
        setFree(&in[i]);
        setFree(&out[i]);
    }
    free(bbs);
    free(gen);
    free(kill);
    free(in);
    free(out);

    return changed;
}


// MAIN OPT FUNCTION
void optimizeFunction(LLVMValueRef function) {
    // Skip function declarations (no body)
    if (LLVMCountBasicBlocks(function) == 0) return;

    // local optimizations - cse on each basic block
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        commonSubExprElim(bb);
    }

    // dead code elimination
    deadCodeElim(function);

    // Constant folding
    constantFolding(function);

    // Dead code elimination again
    deadCodeElim(function);

    // Global optimization: constant propagation + constant folding loop
    bool changed = true;
    while (changed) {
        changed = false;
        if (constantPropagation(function)) {
            changed = true;
            deadCodeElim(function);
        }
        if (constantFolding(function)) {
            changed = true;
            deadCodeElim(function);
        }
    }
}

#ifdef STANDALONE_OPTIMIZER
int main(int argc, char **argv) {
    LLVMModuleRef m;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <input.ll> [output.ll]\n", argv[0]);
        return 1;
    }

    m = createLLVMModel(argv[1]);

    if (m != NULL) {
        // Optimize each function
        for (LLVMValueRef function = LLVMGetFirstFunction(m);
             function;
             function = LLVMGetNextFunction(function)) {
            optimizeFunction(function);
        }

        const char *outfile = (argc == 3) ? argv[2] : "test_new.ll";
        LLVMPrintModuleToFile(m, outfile, NULL);
    } else {
        fprintf(stderr, "m is NULL\n");
        return 1;
    }

    return 0;
}
#endif
