// src/mutators/ir_mutations/swap_operands.cc
#include "src/mutators/ir_mutations/swap_operands.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

static bool isSwappable(const llvm::Instruction &I) {
  return llvm::isa<llvm::BinaryOperator>(I) || llvm::isa<llvm::CmpInst>(I);
}

bool SwapOperands::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (isSwappable(I))
          return true;
  return false;
}

bool SwapOperands::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::Instruction *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (isSwappable(I))
          targets.push_back(&I);

  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, targets.size() - 1);
  auto *I = targets[pick(rng)];

  if (auto *C = llvm::dyn_cast<llvm::CmpInst>(I)) {
    C->swapOperands();
  } else {
    llvm::Value *a = I->getOperand(0);
    llvm::Value *b = I->getOperand(1);
    I->setOperand(0, b);
    I->setOperand(1, a);
  }
  return true;
}

} // namespace regatoni
