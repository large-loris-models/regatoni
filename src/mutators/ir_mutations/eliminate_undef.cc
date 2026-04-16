// src/mutators/ir_mutations/eliminate_undef.cc
#include "src/mutators/ir_mutations/eliminate_undef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

namespace {
struct Site {
  llvm::Instruction *I;
  unsigned op;
};
} // namespace

static bool isUndefLike(const llvm::Value *v) {
  return llvm::isa<llvm::UndefValue>(v) || llvm::isa<llvm::PoisonValue>(v);
}

static bool isEligibleUser(const llvm::Instruction &I) {
  if (llvm::isa<llvm::PHINode>(I))
    return false; // PHI operands pair with BBs; handle specially.
  if (I.isTerminator())
    return false;
  return true;
}

static void collectSites(llvm::Module &M, std::vector<Site> &out) {
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (!isEligibleUser(I))
          continue;
        for (unsigned i = 0, e = I.getNumOperands(); i < e; ++i) {
          if (isUndefLike(I.getOperand(i)))
            out.push_back({&I, i});
        }
      }
    }
  }
}

// Pick a replacement value of the given type that strictly dominates `I`.
// Using DominatorTree (rather than "earlier in same BB") avoids forward
// references across basic blocks. Matches alive-mutate semantics.
static llvm::Value *pickReplacement(llvm::Instruction *I, llvm::Type *ty,
                                    std::mt19937 &rng) {
  std::vector<llvm::Value *> cands;
  auto *F = I->getFunction();
  if (F) {
    for (auto &arg : F->args())
      if (arg.getType() == ty)
        cands.push_back(&arg);
    llvm::DominatorTree DT(*F);
    for (auto &BB : *F) {
      for (auto &J : BB) {
        if (J.getType() != ty)
          continue;
        if (&J == I)
          continue;
        if (DT.dominates(&J, I))
          cands.push_back(&J);
      }
    }
  }
  if (ty->isIntegerTy()) {
    cands.push_back(llvm::ConstantInt::get(ty, 0));
    cands.push_back(llvm::ConstantInt::get(ty, 1));
  } else if (ty->isFloatingPointTy()) {
    cands.push_back(llvm::ConstantFP::get(ty, 0.0));
    cands.push_back(llvm::ConstantFP::get(ty, 1.0));
  } else if (ty->isPointerTy()) {
    cands.push_back(llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ty)));
  } else {
    cands.push_back(llvm::Constant::getNullValue(ty));
  }

  if (cands.empty())
    return nullptr;
  std::uniform_int_distribution<size_t> pick(0, cands.size() - 1);
  return cands[pick(rng)];
}

bool EliminateUndef::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB) {
        if (!isEligibleUser(I))
          continue;
        for (unsigned i = 0, e = I.getNumOperands(); i < e; ++i)
          if (isUndefLike(I.getOperand(i)))
            return true;
      }
  return false;
}

bool EliminateUndef::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<Site> sites;
  collectSites(M, sites);
  if (sites.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, sites.size() - 1);
  auto s = sites[pick(rng)];
  llvm::Type *ty = s.I->getOperand(s.op)->getType();
  llvm::Value *rep = pickReplacement(s.I, ty, rng);
  if (!rep)
    return false;
  s.I->setOperand(s.op, rep);
  return true;
}

} // namespace regatoni
