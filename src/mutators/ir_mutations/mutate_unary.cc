// src/mutators/ir_mutations/mutate_unary.cc
#include "src/mutators/ir_mutations/mutate_unary.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

static bool isFPBinary(const llvm::BinaryOperator &BO) {
  return BO.getType()->isFPOrFPVectorTy();
}

bool MutateUnary::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I))
          if (isFPBinary(*BO))
            return true;
  return false;
}

bool MutateUnary::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::BinaryOperator *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I))
          if (isFPBinary(*BO))
            targets.push_back(BO);

  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, targets.size() - 1);
  auto *BO = targets[pick(rng)];

  std::bernoulli_distribution coin(0.5);
  llvm::Value *operand = BO->getOperand(coin(rng) ? 0 : 1);

  auto *fneg = llvm::UnaryOperator::Create(
      llvm::Instruction::FNeg, operand, "", BO->getIterator());
  fneg->takeName(BO);
  BO->replaceAllUsesWith(fneg);
  BO->eraseFromParent();
  return true;
}

} // namespace regatoni
