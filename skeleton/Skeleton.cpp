#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
using namespace llvm;

namespace
{

  class TSOConsistencyEnforcer : public PassInfoMixin<TSOConsistencyEnforcer>
  {
  public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM)
    {
      bool modified = false;
      auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

      for (Function &F : M) {
        if (F.isDeclaration())
          continue;
        auto& AA = FAM.getResult<AAManager>(F);
        dbgs() << "Function: " << F.getName() << "\n";
        for (BasicBlock &BB : F) {
          for (Instruction &I : BB) {
            if (!isa<LoadInst>(&I) && !isa<StoreInst>(&I)) {
              dbgs() << "First instruction No memory access instruction, moving on to next\n";
              continue; // Skip the current iteration and move to the next instruction
            }
            Instruction *NextInst = I.getNextNode();
            if (NextInst && (isa<LoadInst>(NextInst) || isa<StoreInst>(NextInst))) {
              if (needsFence(&I, NextInst, AA)) {
                dbgs() << "Inserting fence between instructions: \n\t"
                       << I << "\n\t" << *NextInst << "\n";
                insertMemoryFence(NextInst, modified);
              } else {
                dbgs() << "No fence needed between instructions: \n\t"
                       << I << "\n\t" << *NextInst << "\n";
              }
            } else {
              dbgs() << "Second intruction, No memory access instruction, moving on to next\n";
              continue; // Skip the current iteration and move to the next instruction
            }
          }
        }
      }

    if (modified)
    {
      dbgs() << "Modifications made to module: Fences inserted.\n";
    }
    else
    {
      dbgs() << "No modifications made to module: No fences inserted.\n";
    }
    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  private : 
    bool needsFence(Instruction *First, Instruction *Second, AAResults &AA) {
    std::optional<MemoryLocation> FirstLoc = MemoryLocation::getOrNone(First);
    std::optional<MemoryLocation> SecondLoc = MemoryLocation::getOrNone(Second);
    // dbgs() << "Comparing instructions: " << *First << " and " << *Second << "\n";

    if (FirstLoc && SecondLoc) {
      // check alias between memory locations
      if (AA.alias(*FirstLoc, *SecondLoc) != AliasResult::NoAlias) {
        bool isLoadFirst = isa<LoadInst>(First);
        bool isStoreFirst = isa<StoreInst>(First);
        bool isLoadSecond = isa<LoadInst>(Second);
        bool isStoreSecond = isa<StoreInst>(Second);
        // check if instructions are not WR pair
        if ((isLoadFirst && isStoreSecond) || (isStoreFirst && isStoreSecond) || (isLoadFirst && isLoadSecond)) {
          dbgs() << "Inserting fence between: " << *First << " and " << *Second << "\n";
          return true;
        }
      }
    }
    return false;
  }

  void insertMemoryFence(Instruction *Inst, bool &modified)
  {
    IRBuilder<> Builder(Inst);
    auto *Fence = Builder.CreateFence(AtomicOrdering::SequentiallyConsistent, SyncScope::System);
    modified = true;
    dbgs() << "Memory fence inserted: " << *Fence << "\n";
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo(){
  return {
      .APIVersion = LLVM_PLUGIN_API_VERSION,
      .PluginName = "Skeleton pass",
      .PluginVersion = "v0.1",
      .RegisterPassBuilderCallbacks = [](PassBuilder &PB)
      {
        PB.registerPipelineStartEPCallback(
            [](ModulePassManager &MPM, OptimizationLevel Level)
            {
                MPM.addPass(TSOConsistencyEnforcer());
            });
      }};
}
}
