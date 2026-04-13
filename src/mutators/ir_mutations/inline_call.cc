// src/mutators/ir_mutations/inline_call.cc
#include "src/mutators/ir_mutations/inline_call.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <vector>

namespace regatoni {

static bool isInlineableCall(const llvm::CallBase &CB) {
  if (CB.isInlineAsm())
    return false;
  const llvm::Function *callee = CB.getCalledFunction();
  if (!callee)
    return false;
  if (callee->isDeclaration() || callee->isIntrinsic())
    return false;
  // Avoid recursive self-inlining.
  if (callee == CB.getFunction())
    return false;
  return true;
}

bool InlineCall::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I))
          if (isInlineableCall(*CB))
            return true;
  return false;
}

bool InlineCall::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::CallBase *> calls;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I))
          if (isInlineableCall(*CB))
            calls.push_back(CB);

  if (calls.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, calls.size() - 1);
  auto *CB = calls[pick(rng)];

  llvm::InlineFunctionInfo IFI;
  auto res = llvm::InlineFunction(*CB, IFI);
  return res.isSuccess();
}

} // namespace regatoni
