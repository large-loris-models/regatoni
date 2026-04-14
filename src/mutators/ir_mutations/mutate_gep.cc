// src/mutators/ir_mutations/mutate_gep.cc
#include "src/mutators/ir_mutations/mutate_gep.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

bool MutateGep::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (llvm::isa<llvm::GetElementPtrInst>(I))
          return true;
  return false;
}

bool MutateGep::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::GetElementPtrInst *> geps;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *G = llvm::dyn_cast<llvm::GetElementPtrInst>(&I))
          geps.push_back(G);

  if (geps.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, geps.size() - 1);
  auto *G = geps[pick(rng)];

  // Collect constant int indices.
  std::vector<unsigned> constIdx;
  for (unsigned i = 1, e = G->getNumOperands(); i < e; ++i)
    if (llvm::isa<llvm::ConstantInt>(G->getOperand(i)))
      constIdx.push_back(i);

  std::bernoulli_distribution coin(0.5);
  bool mutateIndex = !constIdx.empty() && coin(rng);

  if (mutateIndex) {
    std::uniform_int_distribution<size_t> pickI(0, constIdx.size() - 1);
    unsigned idx = constIdx[pickI(rng)];
    auto *CI = llvm::cast<llvm::ConstantInt>(G->getOperand(idx));
    unsigned bw = CI->getType()->getIntegerBitWidth();
    llvm::APInt cur = CI->getValue();
    std::uniform_int_distribution<int> delta(-2, 2);
    int d = delta(rng);
    if (d == 0)
      d = 1;
    llvm::APInt wide(64, static_cast<uint64_t>(d), /*isSigned=*/true);
    llvm::APInt nvAp = cur.sextOrTrunc(64) + wide;
    nvAp = nvAp.sextOrTrunc(bw);
    llvm::Constant *nv =
        llvm::ConstantInt::get(CI->getContext(), nvAp);
    G->setOperand(idx, nv);
    return true;
  }

  G->setIsInBounds(!G->isInBounds());
  return true;
}

} // namespace regatoni
