// src/mutators/ir_mutations/swap_cmp_predicate.cc
#include "src/mutators/ir_mutations/swap_cmp_predicate.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

using Pred = llvm::CmpInst::Predicate;

static const Pred kICmpPreds[] = {
    Pred::ICMP_EQ,  Pred::ICMP_NE,  Pred::ICMP_SGE, Pred::ICMP_SGT,
    Pred::ICMP_SLE, Pred::ICMP_SLT, Pred::ICMP_UGE, Pred::ICMP_UGT,
    Pred::ICMP_ULE, Pred::ICMP_ULT};

static const Pred kFCmpPreds[] = {
    Pred::FCMP_OEQ, Pred::FCMP_ONE, Pred::FCMP_OGE, Pred::FCMP_OGT,
    Pred::FCMP_OLT, Pred::FCMP_OLE, Pred::FCMP_ORD, Pred::FCMP_UNO,
    Pred::FCMP_UEQ, Pred::FCMP_UNE, Pred::FCMP_UGE, Pred::FCMP_UGT,
    Pred::FCMP_ULT, Pred::FCMP_ULE};

bool SwapCmpPredicate::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (llvm::isa<llvm::CmpInst>(I))
          return true;
  return false;
}

bool SwapCmpPredicate::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::CmpInst *> cmps;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *C = llvm::dyn_cast<llvm::CmpInst>(&I))
          cmps.push_back(C);

  if (cmps.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, cmps.size() - 1);
  auto *target = cmps[pick(rng)];

  bool isFP = target->getOperand(0)->getType()->isFPOrFPVectorTy();
  const Pred *preds = isFP ? kFCmpPreds : kICmpPreds;
  size_t n = isFP ? (sizeof(kFCmpPreds) / sizeof(Pred))
                  : (sizeof(kICmpPreds) / sizeof(Pred));

  std::uniform_int_distribution<size_t> pd(0, n - 1);
  Pred newP;
  do {
    newP = preds[pd(rng)];
  } while (newP == target->getPredicate());

  target->setPredicate(newP);
  return true;
}

} // namespace regatoni
