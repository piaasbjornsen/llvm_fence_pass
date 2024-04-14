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
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"

using namespace llvm;

namespace {
    struct Edge {
        Instruction* first;
        Instruction* second;
    };

    class MemoryDependencyGraph {
    public:
        //tree of dependency edges
        std::vector<Edge> edges;
        //use setvector to store instructions and avoid duplicates
        using InstSet = SetVector<Instruction*>;
        //graph is a map from an Instruction* to a set of Instruction*
        DenseMap<Instruction*, InstSet> graph;  

        //adds dependency as edge between two input instructions
        void addDependency(Instruction* first, Instruction* second) {
            graph[first].insert(second);
        }

        //return a vector with all edges in the graph
        std::vector<Edge> getAllEdges() {
            edges.clear();  //clear existing vector and construct a new one from the graph
            for (auto& pair : graph) {
                Instruction* first = pair.first;
                InstSet& seconds = pair.second;
                for (Instruction* second : seconds) {
                    edges.push_back({ first, second }); //add edge to list of edges
                }
            }
            return edges;
        }

        //checks if there is a dependency between two instructions
        bool hasDependency(Instruction* first, Instruction* second) {
            return graph[first].count(second) > 0;
        }
    };

    class TSOConsistencyEnforcer : public PassInfoMixin<TSOConsistencyEnforcer> {
    public:
        PreservedAnalyses run(Module& M, ModuleAnalysisManager& AM) {
            bool modified = false;
            auto& FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

            MemoryDependencyGraph MDG;

            for (Function& F : M) {
                if (F.isDeclaration()) continue;
                dbgs() << "Function: " << F.getName() << "\n";
                auto& AA = FAM.getResult<AAManager>(F);
                for (BasicBlock& BB : F) {
                    //check if initial instruction is store/load
                    for (Instruction& I : BB) {
                        if (isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
                            //compare all pairs in the basic block
                            for (Instruction& J : BB) {
                                if (&I != &J && (isa<LoadInst>(&J) || isa<StoreInst>(&J))) {
                                    if (needsFence(&I, &J, AA)) {
                                        MDG.addDependency(&I, &J);
                                        dbgs() << "Dependency detected between: " << I << " and " << J << "\n";
                                    }
                                }
                            }
                        }
                    }
                }
            }

            for (auto& Edge : MDG.getAllEdges()) {
                //check if edge is a dependency and if it needs a fence
                if (needsFence(Edge.first, Edge.second, FAM.getResult<AAManager>(*Edge.first->getFunction())) 
                    && MDG.hasDependency(Edge.first, Edge.second)) {
                    //remove dependency and insert fence
                    dbgs() << "Inserting fence between: " << *Edge.first << " and " << *Edge.second << "\n";
					insertMemoryFence(Edge.second, modified);
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
