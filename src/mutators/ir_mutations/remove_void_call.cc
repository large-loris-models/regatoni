// src/mutators/ir_mutations/remove_void_call.cc
#include "src/mutators/ir_mutations/remove_void_call.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

static bool isRemovable(const llvm::CallInst &CI) {
  if (CI.isInlineAsm())
    return false;
  if (!CI.getType()->isVoidTy())
    return false;
  // A void call can't have users, but be defensive.
  return CI.use_empty();
}

bool RemoveVoidCall::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (auto *CI = llvm::dyn_cast<llvm::CallInst>(&I))
          if (isRemovable(*CI))
            return true;
  return false;
}

bool RemoveVoidCall::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::CallInst *> calls;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *CI = llvm::dyn_cast<llvm::CallInst>(&I))
          if (isRemovable(*CI))
            calls.push_back(CI);

  if (calls.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, calls.size() - 1);
  calls[pick(rng)]->eraseFromParent();
  return true;
}

} // namespace regatoni
