// src/mutators/ir_mutations/modify_flags.cc
#include "src/mutators/ir_mutations/modify_flags.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include <vector>

namespace regatoni {

static bool hasAnyFlags(const llvm::Instruction &I) {
  return llvm::isa<llvm::OverflowingBinaryOperator>(I) ||
         llvm::isa<llvm::PossiblyExactOperator>(I) ||
         llvm::isa<llvm::FPMathOperator>(I);
}

bool ModifyFlags::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (hasAnyFlags(I))
          return true;
  return false;
}

bool ModifyFlags::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::Instruction *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (hasAnyFlags(I))
          targets.push_back(&I);

  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, targets.size() - 1);
  auto *I = targets[pick(rng)];
  std::bernoulli_distribution coin(0.5);
  bool changed = false;

  if (auto *OBO = llvm::dyn_cast<llvm::OverflowingBinaryOperator>(I)) {
    (void)OBO;
    auto *BO = llvm::cast<llvm::BinaryOperator>(I);
    bool nsw = coin(rng), nuw = coin(rng);
    if (BO->hasNoSignedWrap() != nsw || BO->hasNoUnsignedWrap() != nuw) {
      BO->setHasNoSignedWrap(nsw);
      BO->setHasNoUnsignedWrap(nuw);
      changed = true;
    }
  }

  if (auto *PE = llvm::dyn_cast<llvm::PossiblyExactOperator>(I)) {
    (void)PE;
    auto *BO = llvm::cast<llvm::BinaryOperator>(I);
    bool ex = coin(rng);
    if (BO->isExact() != ex) {
      BO->setIsExact(ex);
      changed = true;
    }
  }

  if (llvm::isa<llvm::FPMathOperator>(I)) {
    llvm::FastMathFlags flags;
    flags.setAllowContract(coin(rng));
    flags.setAllowReassoc(coin(rng));
    flags.setAllowReciprocal(coin(rng));
    flags.setApproxFunc(coin(rng));
    flags.setNoInfs(coin(rng));
    flags.setNoNaNs(coin(rng));
    flags.setNoSignedZeros(coin(rng));
    auto old = I->getFastMathFlags();
    if (old != flags) {
      I->setFastMathFlags(flags);
      changed = true;
    }
  }

  return changed;
}

} // namespace regatoni
