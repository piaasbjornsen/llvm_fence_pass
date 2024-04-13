#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/AliasAnalysis.h"

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
                            IRBuilder<> builder(op);
                            errs() << "Found a binary operator: ";
                            op->print(errs());
                            errs() << "\n";

                            Value *lhs = op->getOperand(0);
                            Value *rhs = op->getOperand(1);
                            errs() << "lhs: ";
                            lhs->print(errs());
                            errs() << "\n";
                            errs() << "rhs: ";
                            rhs->print(errs());
                            errs() << "\n";
                            Value *mul = builder.CreateMul(lhs, rhs);

                            for (auto &U : op->uses()) {
                                User *user = U.getUser();
                                user->setOperand(U.getOperandNo(), mul);
                            }

                            return PreservedAnalyses::none();
                        }
                    }
                }
            }
            return PreservedAnalyses::all();
        }
    };

    class TSOConsistencyEnforcer : public PassInfoMixin<TSOConsistencyEnforcer> {
    public:
        static constexpr int LookaheadLimit = 5; // Check up to 5 instructions ahead

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
            bool modified = false;
            AliasAnalysis *AA = nullptr;

            try {
                AA = &AM.getResult<AAManager>(M);
            } catch (const std::exception& e) {
                errs() << "Failed to get AAManager: " << e.what() << "\n";
                return PreservedAnalyses::all(); // If AA is not available, skip the pass
            }

            for (Function &F : M) {
                if (F.isDeclaration()) continue;

                dbgs() << "Function: " << F.getName() << "\n";
                for (BasicBlock &BB : F) {
                    for (Instruction &I : BB) {
                        Instruction* limit = getNextInstruction(&I, LookaheadLimit, &BB);
                        for (Instruction* NextInst = I.getNextNode(); NextInst != nullptr && NextInst != limit; NextInst = NextInst->getNextNode()) {
                            if (isa<LoadInst>(NextInst) || isa<StoreInst>(NextInst)) {
                                if (needsFence(&I, NextInst, *AA)) {
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

                if (modified) {
                    dbgs() << "Modifications made to module: Fences inserted.\n";
                } else {
                    dbgs() << "No modifications made to module: No fences inserted.\n";
                }

                return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
            }
            // Missing return statement here
            return PreservedAnalyses::all(); // Added return statement
        }
    private:

        Instruction* getNextInstruction(Instruction* start, int limit, BasicBlock* BB) {
            Instruction* current = start;
            for (int i = 0; i < limit && current != nullptr && current->getNextNode() != nullptr; i++) {
                current = current->getNextNode();
            }
            return current;
        }

        void insertMemoryFence(Instruction *Inst, bool &modified) {
            IRBuilder<> Builder(Inst);
            auto *Fence = Builder.CreateFence(AtomicOrdering::SequentiallyConsistent, SyncScope::System);
            modified = true;

            dbgs() << "Memory fence inserted: " << *Fence << "\n";
        }

        bool needsFence(Instruction *First, Instruction *Second, AliasAnalysis &AA) {
            bool isFirstLoad = isa<LoadInst>(First);
            bool isFirstStore = isa<StoreInst>(First);
            bool isSecondLoad = isa<LoadInst>(Second);
            bool isSecondStore = isa<StoreInst>(Second);


            // Ensure early return if neither instruction is a load or store
            if ((!isFirstLoad && !isFirstStore) || (!isSecondLoad && !isSecondStore)) {
                errs() << "Not both instructions are memory accesses.\n";
                return false;
            }

            auto MemLocFirst = MemoryLocation::getOrNone(First);
            auto MemLocSecond = MemoryLocation::getOrNone(Second);
            
            // Ensure early return if either instruction does not reference memory
            if (!MemLocFirst.has_value() || !MemLocSecond.has_value()) {
                errs() << "One of the instructions does not reference memory.\n";
                return false;
            }

            // Analyze the type of memory access and aliasing
            if (isFirstLoad && isSecondStore) {
                errs() << "Detected RW pair.\n";
                return AA.alias(MemLocFirst.value(), MemLocSecond.value()) == AliasResult::MustAlias;
            } else if (isFirstStore && isSecondLoad) {
                errs() << "Detected WR pair (allowed under TSO).\n";
                return false;
            } else if (isFirstStore && isSecondStore) {
                errs() << "Detected WW pair.\n";
                return AA.alias(MemLocFirst.value(), MemLocSecond.value()) == AliasResult::MustAlias;
            } else if (isFirstLoad && isSecondLoad) {
                errs() << "Detected RR pair.\n";
                return AA.alias(MemLocFirst.value(), MemLocSecond.value()) == AliasResult::MustAlias;
            }

            // Default return if no conditions are met (safety net)
            errs() << "No alias or no need for a fence based on the TSO model.\n";
            return false;
        }

    };

   extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
        return {
            .APIVersion = LLVM_PLUGIN_API_VERSION,
            .PluginName = "Skeleton pass",
            .PluginVersion = "v0.1",
            .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
                PB.registerAnalysisRegistrationCallback([](FunctionAnalysisManager &FAM) {
                    FAM.registerPass([] {
                        errs() << "Registering AAManager\n";
                        return AAManager();
                    });
                });

                PB.registerPipelineStartEPCallback([](ModulePassManager &MPM, OptimizationLevel Level) {
                    MPM.addPass(RequireAnalysisPass<AAManager, Module>());
                });

                PB.registerPipelineParsingCallback(
                    [&](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
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
}
