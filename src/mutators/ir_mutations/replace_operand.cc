// src/mutators/ir_mutations/replace_operand.cc
#include "src/mutators/ir_mutations/replace_operand.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

static bool isTarget(const llvm::Instruction &I) {
  return llvm::isa<llvm::BinaryOperator>(I) || llvm::isa<llvm::CmpInst>(I);
}

// Collect values that (1) are of `ty` and (2) are legal to use as an operand
// of `I` — i.e., they strictly dominate `I`. This mirrors alive-mutate's
// getRandomDominatedInstruction (tools/mutator-utils/mutator.cpp:518).
//
// Without a dominance check, a candidate picked from a later-defined
// instruction would form a forward reference (use-before-def) and break
// SSA, which hangs `opt` on subsequent optimization passes.
static void collectDominatingCandidates(llvm::Instruction *I,
                                        llvm::DominatorTree &DT,
                                        llvm::Type *ty,
                                        std::vector<llvm::Value *> &out) {
  auto *F = I->getFunction();
  if (!F)
    return;

  // Function arguments always dominate every instruction in F.
  for (auto &arg : F->args())
    if (arg.getType() == ty)
      out.push_back(&arg);

  // Every instruction whose definition strictly dominates I.
  // DominatorTree::dominates(Inst, Inst) is strict: it returns false for
  // (I, I), so I itself is correctly excluded.
  for (auto &BB : *F) {
    for (auto &J : BB) {
      if (J.getType() != ty)
        continue;
      if (&J == I)
        continue;
      if (DT.dominates(&J, I))
        out.push_back(&J);
    }
  }

  // Simple constants are always legal.
  if (ty->isIntegerTy()) {
    out.push_back(llvm::ConstantInt::get(ty, 0));
    out.push_back(llvm::ConstantInt::get(ty, 1));
    out.push_back(llvm::ConstantInt::getSigned(ty, -1));
  } else if (ty->isFloatingPointTy()) {
    out.push_back(llvm::ConstantFP::get(ty, 0.0));
    out.push_back(llvm::ConstantFP::get(ty, 1.0));
  }
}

bool ReplaceOperand::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (isTarget(I))
          return true;
  return false;
}

bool ReplaceOperand::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::Instruction *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (isTarget(I))
          targets.push_back(&I);

  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pickI(0, targets.size() - 1);
  auto *I = targets[pickI(rng)];

  // Build a dominator tree for I's function. Rebuilding per call is O(N)
  // and negligible next to `opt -O2` / Alive2 costs downstream.
  llvm::DominatorTree DT(*I->getFunction());

  // Try both operand positions in a random order.
  std::uniform_int_distribution<unsigned> pickPos(0, 1);
  unsigned first = pickPos(rng);
  for (unsigned k = 0; k < 2; ++k) {
    unsigned pos = (first + k) & 1u;
    llvm::Value *cur = I->getOperand(pos);
    std::vector<llvm::Value *> cands;
    collectDominatingCandidates(I, DT, cur->getType(), cands);

    // Remove the current value from candidates.
    std::vector<llvm::Value *> filtered;
    filtered.reserve(cands.size());
    for (auto *v : cands)
      if (v != cur)
        filtered.push_back(v);

    if (filtered.empty())
      continue;

    std::uniform_int_distribution<size_t> pickV(0, filtered.size() - 1);
    I->setOperand(pos, filtered[pickV(rng)]);
    return true;
  }

  return false;
}

} // namespace regatoni
