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

                //iterate over all blocks, then instructions in the function
                for (BasicBlock& BB : F) {
                    for (Instruction& I : BB) {

                        //skip if the instruction is not a load or store
                        Instruction* NextInst = I.getNextNode();
                        if (NextInst && (isa<LoadInst>(&I) || isa<StoreInst>(&I)) &&
                            (isa<LoadInst>(NextInst) || isa<StoreInst>(NextInst))) {

                            //check if a fence is needed between the two instructions
                            checkForReordering(&I, F, AA, modified);
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
        }

    private:
        void insertMemoryFence(Instruction* Inst, bool& modified) {
            //add a fence after the instruction
            IRBuilder<> Builder(Inst);
            auto* Fence = Builder.CreateFence(AtomicOrdering::SequentiallyConsistent, SyncScope::System);
            modified = true;

            dbgs() << "Memory fence inserted: " << *Fence << "\n";
        }

        bool needsFence(Instruction* First, Instruction* Second, AAResults& AA, int maxDistance) {
            int distance = 0;
            Instruction* nextInst = First->getNextNode();
            //check if the two instructions are within a certain distance
            while (nextInst != nullptr && nextInst != Second) {
                nextInst = nextInst->getNextNode();
                distance++;
                if (distance > maxDistance) return false;
            }
            //check if the two instructions alias using AA
            if (AA.alias(MemoryLocation::get(First), MemoryLocation::get(Second)) != AliasResult::NoAlias) {
                bool isFirstLoad = isa<LoadInst>(First);
                bool isSecondLoad = isa<LoadInst>(Second);
                bool isFirstStore = isa<StoreInst>(First);
                bool isSecondStore = isa<StoreInst>(Second);
                //check for potential non-WR reordering
                if ((isFirstLoad && isSecondStore) || // RW
                    (isFirstStore && isSecondStore) || // WW
                    (isFirstLoad && isSecondLoad)) {   // RR
                    return true;
                }
            }
            return false;
        }

        void checkForReordering(Instruction* Inst, Function& F, AAResults& AA, bool modified) {
            //find next instruction in the same basic block
            Instruction* Next = Inst->getNextNode();
            while (Next) {
                if (isa<LoadInst>(Next) || isa<StoreInst>(Next)) {
                    //check if these two instructions might need a fence, specify max distance as int
                    if (needsFence(Inst, Next, AA, 10)) {
                        dbgs() << "Potential reordering found between:\n\t"
                            << *Inst << "\n\t" << *Next << "\n";
                        //insert a fence after the first instruction
                        insertMemoryFence(Next, modified);
                    }
                    else {
						dbgs() << "No reordering found between:\n\t"
							<< *Inst << "\n\t" << *Next << "\n";
                    }
                }
                Next = Next->getNextNode();
            }
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
