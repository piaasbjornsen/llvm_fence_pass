#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace {

class MemoryFenceInsertion : public PassInfoMixin<MemoryFenceInsertion> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool modified = false;

    for (Function &F : M) {
      if (F.isDeclaration()) continue; // Skip function declarations

      dbgs() << "\nAnalyzing function: " << F.getName() << "\n";

      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          // Start grouping
          dbgs() << "\n---\n"; 

          if (auto *LI = dyn_cast<LoadInst>(&I)) {
            dbgs() << "Found Load Instruction: " << *LI << "\n";
            checkMemoryAccessPair(LI, modified);
          } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
            dbgs() << "Found Store Instruction: " << *SI << "\n";
            checkMemoryAccessPair(SI, modified);
          }

          // End grouping
          dbgs() << "---\n";
        }
      }
    }

    if (modified) {
      dbgs() << "Module modified with memory fences\n\n";
    } else {
      dbgs() << "No modifications made to module\n\n";
    }

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
  
private:
  bool arePotentiallyDifferentMemoryLocations(Instruction *Inst1, Instruction *Inst2) {
    Value *Op1 = Inst1->getOperand(0);
    Value *Op2 = Inst2->getOperand(0);
    bool potentiallyDifferent = Op1 != Op2;
    dbgs() << "Comparing memory locations: " << *Op1 << " and " << *Op2 << " - Potentially Different? " << (potentiallyDifferent ? "Yes" : "No") << "\n";
    return potentiallyDifferent;
  }

  void checkMemoryAccessPair(Instruction *I, bool &modified) {
    Instruction *NextInst = I->getNextNode();
    if (NextInst && (isa<LoadInst>(NextInst) || isa<StoreInst>(NextInst))) {
      dbgs() << "Checking memory access pair: " << *I << " and " << *NextInst << "\n";
      if (arePotentiallyDifferentMemoryLocations(I, NextInst)) {
        insertMemoryFence(I, modified);
      }
    }
  }

  void insertMemoryFence(Instruction *Inst, bool &modified) {
    IRBuilder<> Builder(Inst->getNextNode()); // Insert after the instruction

    Builder.CreateFence(AtomicOrdering::SequentiallyConsistent, SyncScope::System);
    modified = true;

    dbgs() << "Inserted memory fence after instruction: " << *Inst << "\n";
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "MemoryFenceInsertion", "v0.1",
        [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                    MPM.addPass(MemoryFenceInsertion());
                });
        }
    };
}

} // namespace
