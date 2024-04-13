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
            //define function analysis manager (helps to get AA for each function)
            auto& FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

            for (Function& F : M) {
                //skip declarations since they don't have instructions
                if (F.isDeclaration()) continue;

                dbgs() << "Function: " << F.getName() << "\n";

                //define alias analysis for current function
                auto& AA = FAM.getResult<AAManager>(F);
                for (BasicBlock& BB : F) {
                    for (Instruction& I : BB) {
                        //skip if the instruction is not a load or store
                        Instruction* NextInst = I.getNextNode();
                        if (NextInst && (isa<LoadInst>(&I) || isa<StoreInst>(&I)) &&
                            (isa<LoadInst>(NextInst) || isa<StoreInst>(NextInst))) {
                            //check if a fence is needed between the two instructions
                            if (needsFence(&I, NextInst, AA)) {
                                dbgs() << "Inserting fence between instructions: \n\t"
                                    << I << "\n\t" << *NextInst << "\n";
                                insertMemoryFence(NextInst, modified);
                            }
                            else {
                                dbgs() << "No fence needed between instructions: \n\t"
                                    << I << "\n\t" << *NextInst << "\n";
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
            //Check if the two instructions alias using AA
            if (AA.alias(MemoryLocation::get(First), MemoryLocation::get(Second)) != AliasResult::NoAlias) {
                bool isFirstLoad = isa<LoadInst>(First);
                bool isSecondLoad = isa<LoadInst>(Second);
                bool isFirstStore = isa<StoreInst>(First);
                bool isSecondStore = isa<StoreInst>(Second);
                //Check for potential non-WR reordering
                if ((isFirstLoad && isSecondStore) || // RW
                    (isFirstStore && isSecondStore) || // WW
                    (isFirstLoad && isSecondLoad)) {   // RR
                    return true;
                }
            }
            return false;
        }

        void insertMemoryFence(Instruction* Inst, bool& modified) {
            //Add a fence after the instruction
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
