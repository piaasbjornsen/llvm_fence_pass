#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace {

class MemoryFenceInsertion : public PassInfoMixin<MemoryFenceInsertion> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool modified = false; // Flag to track if the module was modified

    // Iterate over each function in the module
    for (Function &F : M) {
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          dbgs() << "New iteration -------\n";

          // Check if instruction is a memory access instruction 
          if (isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
            dbgs() << "Load or store\n";

            // Insert memory fence here or perform any necessary modifications
            // For example:
            // InsertFence(&I);
            
            modified = true; // Set modified flag to true
          }
        }
      }
    }

    if (modified) {
      dbgs() << "Module modified\n";
    } else {
      dbgs() << "Module not modified\n";
    }

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        .APIVersion = LLVM_PLUGIN_API_VERSION,
        .PluginName = "Memory Fence Insertion Pass",
        .PluginVersion = "v0.1",
        .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                    MPM.addPass(MemoryFenceInsertion());
                });
        }
    };
}
