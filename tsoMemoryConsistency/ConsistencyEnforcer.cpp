#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

namespace {
class ConsistencyEnforcer : public PassInfoMixin<ConsistencyEnforcer> {
public:
  static constexpr int LookaheadLimit = 5; // Check up to 5 instructions ahead

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool modified = false;
    int instructionNumber = 0;
    int vsNumber = 0;


    for (Function &F : M) {
      if (F.isDeclaration()) continue;

      dbgs() << "Function: " << F.getName() << "\n";
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          ++instructionNumber; // Increment instruction number
          // Print the instruction number (starting at 1) and indent
          dbgs() << " \n" << "START instruction: " << instructionNumber << "    is" << I << ": \n";
          // Check if the instruction is a load or store
          if (!isa<LoadInst>(&I) && !isa<StoreInst>(&I)) {
            dbgs() << "Not a memory access instruction \n";
            dbgs() << "End instruction: " << instructionNumber << "\n";
            continue; // Skip further processing and move to the next instruction
            } else {
            dbgs() << "Memory access instruction, checking for pairs... \n";

            }
        Instruction* limit = getNextInstruction(&I, LookaheadLimit, &BB);
          for (Instruction* NextInst = I.getNextNode(); NextInst != nullptr && NextInst != limit; NextInst = NextInst->getNextNode()) {
            ++vsNumber; // Increment instruction number
            dbgs() << "\tvs " << vsNumber << "\n";
            if (isa<LoadInst>(NextInst) || isa<StoreInst>(NextInst)) {
              if (needsFence(&I, NextInst)) {
                dbgs() << "\t Inserting fence between instructions: \n\t"
                       << I << "\n\t   " << *NextInst << "\n";
                insertMemoryFence(NextInst, modified);
              } else {
                dbgs() << "\t No fence needed between instructions: \n\t   "
                       << I << "\n\t   " << *NextInst << "\n";
              }
            } else {
                 dbgs() << "\t Not a memory access instruction \n";
            }
          }
            vsNumber = 0;
            dbgs() << "End instruction: " << instructionNumber << "\n\n";

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
  // Identify if the instructions are load or store
  bool isFirstLoad = isa<LoadInst>(First);
  bool isSecondLoad = isa<LoadInst>(Second);
  bool isSecondStore = isa<StoreInst>(Second);

  // Check if both instructions access memory
  if (isFirstLoad || isSecondLoad || isSecondStore) {
    Value *FirstMemOperand = nullptr;
    Value *SecondMemOperand = nullptr;

    if (isFirstLoad) {
        FirstMemOperand = cast<LoadInst>(First)->getPointerOperand();
    } else if (isa<StoreInst>(First)) {
        FirstMemOperand = cast<StoreInst>(First)->getPointerOperand();
    }

    if (isSecondLoad) {
        SecondMemOperand = cast<LoadInst>(Second)->getPointerOperand();
    } else if (isa<StoreInst>(Second)) {
        SecondMemOperand = cast<StoreInst>(Second)->getPointerOperand();
    }

    dbgs() << "\t Comparing memory addresses: \n";
    dbgs() << "\t    FirstMemOperand = " << FirstMemOperand << "\n";
    dbgs() << "\t    SecondMemOperand = " << SecondMemOperand  << "\n";

    // Compare memory operands to check if they access the same memory address
    if (FirstMemOperand == SecondMemOperand) {
      dbgs() << "\t Detected access to the same memory address.\n";
      if (isFirstLoad && isSecondStore) {
          dbgs() << "\t Detected RW pair requiring a fence.\n";
          return true;
      } else if (!isFirstLoad && isSecondStore) {
          dbgs() << "\t Detected WW pair requiring a fence.\n";
          return true;
      } else if (isFirstLoad && isSecondLoad) {
          dbgs() << "\t Detected RR pair, usually not requiring a fence under TSO.\n";
      } else {
          dbgs() << "\t Detected WR pair, allowed under TSO without a fence.\n";
      }
    } else {
      dbgs() << "\t No fence needed as instructions access different memory addresses.\n";
    }
  } else {
    dbgs() << "\t Instructions do not form a memory access pair.\n";
  }
  return false;
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
        .PluginName = "ConsistencyEnforcer pass",
        .PluginVersion = "v0.1",
        .RegisterPassBuilderCallbacks = [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "enforce-tso") {
                        MPM.addPass(ConsistencyEnforcer());
                        return true;
                    } 
                    return false;
                });
        }
    };
}