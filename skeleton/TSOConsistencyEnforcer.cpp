#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace llvm;

namespace {

    class TSOConsistencyEnforcer : public PassInfoMixin<TSOConsistencyEnforcer> {
    public:
        PreservedAnalyses run(Module& M, ModuleAnalysisManager& AM) {
            bool modified = false;
            auto& FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

            for (Function& F : M) {
                if (F.isDeclaration()) continue;
                dbgs() << "Function: " << F.getName() << "\n";
                auto& AA = FAM.getResult<AAManager>(F);
                for (BasicBlock& BB : F) {
                    for (Instruction& I : BB) {
                        //compare all store-load pairs in the basic block
                        for (Instruction& J : BB) {
                            if (&I != &J && (isa<LoadInst>(&J) || isa<StoreInst>(&J))) {
                                if (needsFence(&I, &J, AA)) {
                                    insertMemoryFence(&J, modified);
                                }
                            }
                        }
                    }
                }
            }

            if (modified) {
                dbgs() << "Modifications made to module: Fences inserted.\n";
            }
            else {
                dbgs() << "No modifications made to module: No fences inserted.\n";
            }
            return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
        }

    private:
        bool needsFence(Instruction* First, Instruction* Second, AAResults& AA) {
            Optional<MemoryLocation> FirstLoc = MemoryLocation::getOrNone(First);
            Optional<MemoryLocation> SecondLoc = MemoryLocation::getOrNone(Second);
            //dbgs() << "Comparing instructions: " << *First << " and " << *Second << "\n";

            if (FirstLoc && SecondLoc) {
                //check alias between memory locations
                if (AA.alias(*FirstLoc, *SecondLoc) != AliasResult::NoAlias) {
                    bool isLoadFirst = isa<LoadInst>(First);
                    bool isStoreFirst = isa<StoreInst>(First);
                    bool isLoadSecond = isa<LoadInst>(Second);
                    bool isStoreSecond = isa<StoreInst>(Second);
                    //check if instructions are not WR pair
                    if ((isLoadFirst && isStoreSecond) || (isStoreFirst && isStoreSecond) || (isLoadFirst && isLoadSecond)) {
                        dbgs() << "Inserting fence between: " << *First << " and " << *Second << "\n";
						return true;
					}                
                }
            }
            return false;
        }

        void insertMemoryFence(Instruction* Inst, bool& modified) {
            IRBuilder<> Builder(Inst);
            auto* Fence = Builder.CreateFence(AtomicOrdering::SequentiallyConsistent, SyncScope::System);
            modified = true;
            dbgs() << "Memory fence inserted: " << *Fence << "\n";
        }
    };


	extern "C" LLVM_ATTRIBUTE_WEAK::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
		return {
			LLVM_PLUGIN_API_VERSION, "MemoryFenceInsertion", "v0.1",
			[](PassBuilder& PB) {
				PB.registerPipelineStartEPCallback(
					[](ModulePassManager& MPM, OptimizationLevel Level) {
						MPM.addPass(TSOConsistencyEnforcer());
					});
			}
		};
	}

} // namespace
