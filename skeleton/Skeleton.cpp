#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

namespace {
    struct SkeletonPass : public PassInfoMixin<SkeletonPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        for (auto &F : M.functions()) {
            errs() << "In a function called " << F.getName() << "!\n";
            errs() << "Function body:\n";
            F.print(errs());

            for (auto &B : F) {
              errs() << "Basic block:\n";
              B.print(errs());

                for (auto &I : B) {
                  errs() << "Instruction: \n";
                  I.print(errs(), true);
                  errs() << "\n";
                  if (auto *op = dyn_cast<BinaryOperator>(&I)) {          
                      // Insert at the point where the instruction `op`
                      // appears.
                      IRBuilder<> builder(op);
                      errs() << "Found a binary operator: ";
                      op->print(errs());
                      errs() << "\n";

                      // Make a multiply with the same operands as `op`.
                      Value *lhs = op->getOperand(0);
                      Value *rhs = op->getOperand(1);
                      errs() << "lhs: ";
                      lhs->print(errs());
                      errs() << "\n";
                      errs() << "rhs: ";
                      rhs->print(errs());
                      errs() << "\n";
                      Value *mul = builder.CreateMul(lhs, rhs);

                      // Everywhere the old instruction was used as an
                      // operand, use our new multiply instruction instead.
                      for (auto &U : op->uses()) {
                        // A User is anything with operands.
                        User *user = U.getUser();
                        user->setOperand(U.getOperandNo(), mul);
                      }

                      // We modified the code.
                      return PreservedAnalyses::none();
                    }
                }
            }
        }
        return PreservedAnalyses::all();
    };
};

class TSOConsistencyEnforcer : public PassInfoMixin<TSOConsistencyEnforcer> {
public:
  static constexpr int LookaheadLimit = 5; // Check up to 5 instructions ahead

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool modified = false;

    for (Function &F : M) {
      if (F.isDeclaration()) continue;

      dbgs() << "Function: " << F.getName() << "\n";
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          Instruction* limit = getNextInstruction(&I, LookaheadLimit, &BB);
          for (Instruction* NextInst = I.getNextNode(); NextInst != nullptr && NextInst != limit; NextInst = NextInst->getNextNode()) {
            if (isa<LoadInst>(NextInst) || isa<StoreInst>(NextInst)) {
               if (needsFence(&I, NextInst)) {
                dbgs() << "Inserting fence between instructions: \n\t"
                       << I << "\n\t" << *NextInst << "\n";
                insertMemoryFence(NextInst, modified);
              } else {
                dbgs() << "No fence needed between instructions: \n\t"
                       << I << "\n\t" << *NextInst << "\n";
              }
            }
          }
        }
      }
    }

    if (modified) {
      dbgs() << "Modifications made to module: Fences inserted.\n";
    } else {
      dbgs() << "No modifications made to module: No fences inserted.\n";
    }

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
private:

  Instruction* getNextInstruction(Instruction* start, int limit, BasicBlock* BB) {
                Instruction* current = start;
                for (int i = 0; i < limit && current != nullptr && current->getNextNode() != nullptr; i++) {
                    current = current->getNextNode();
                }
                return current;
  }
  bool needsFence(Instruction *First, Instruction *Second) {
    // TSO allows WR without fences; fences are needed for RW, WW, and RR pairs
    bool isFirstLoad = isa<LoadInst>(First);
    bool isSecondLoad = isa<LoadInst>(Second);
    bool isSecondStore = isa<StoreInst>(Second);

    if (isFirstLoad && isSecondStore) {
      dbgs() << "Detected RW pair.\n";
    } else if (!isFirstLoad && isSecondStore) {
      dbgs() << "Detected WW pair.\n";
    } else if (isFirstLoad && isSecondLoad) {
      dbgs() << "Detected RR pair.\n";
    } else {
      dbgs() << "Detected WR pair (allowed under TSO).\n";
    }

    return (isFirstLoad && isSecondStore) || (!isFirstLoad && isSecondStore) || (isFirstLoad && isSecondLoad);
  }
  

  void insertMemoryFence(Instruction *Inst, bool &modified) {
    IRBuilder<> Builder(Inst);
    auto *Fence = Builder.CreateFence(AtomicOrdering::SequentiallyConsistent, SyncScope::System);
    modified = true;

    dbgs() << "Memory fence inserted: " << *Fence << "\n";
  }
};

}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        .APIVersion = LLVM_PLUGIN_API_VERSION,
        .PluginName = "Skeleton pass",
        .PluginVersion = "v0.1",
        .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "enforce-tso") {
                        MPM.addPass(TSOConsistencyEnforcer());
                        return true;
                    } else if (Name == "example") {
                        MPM.addPass(SkeletonPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}