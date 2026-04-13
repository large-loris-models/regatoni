// src/mutators/ir_mutations/move_instruction.cc
#include "src/mutators/ir_mutations/move_instruction.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

static bool isMovable(const llvm::Instruction &I) {
  if (I.isTerminator() || llvm::isa<llvm::PHINode>(I))
    return false;
  if (I.mayReadOrWriteMemory())
    return false;
  if (llvm::isa<llvm::CallBase>(I))
    return false;
  return true;
}

bool MoveInstruction::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (isMovable(I))
          return true;
  return false;
}

bool MoveInstruction::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::Instruction *> candidates;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (isMovable(I))
          candidates.push_back(&I);

  if (candidates.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
  auto *I = candidates[pick(rng)];
  auto *BB = I->getParent();

  // Flatten BB instructions into a vector for index math.
  std::vector<llvm::Instruction *> insts;
  insts.reserve(BB->size());
  for (auto &J : *BB)
    insts.push_back(&J);

  // Current index of I.
  size_t curIdx = 0;
  for (; curIdx < insts.size(); ++curIdx)
    if (insts[curIdx] == I)
      break;

  // Lower bound: must come after any PHIs and after any operand defined
  // in this BB.
  size_t lo = 0;
  for (size_t k = 0; k < insts.size(); ++k) {
    if (llvm::isa<llvm::PHINode>(insts[k]))
      lo = k + 1;
  }
  for (unsigned op = 0; op < I->getNumOperands(); ++op) {
    if (auto *D = llvm::dyn_cast<llvm::Instruction>(I->getOperand(op))) {
      if (D->getParent() != BB)
        continue;
      for (size_t k = 0; k < insts.size(); ++k)
        if (insts[k] == D && k + 1 > lo)
          lo = k + 1;
    }
  }

  // Upper bound: must come before any user in this BB, and before the
  // terminator. hi is the first index where I may NOT land.
  size_t hi = insts.size() - 1; // terminator index
  for (auto *U : I->users()) {
    auto *UI = llvm::dyn_cast<llvm::Instruction>(U);
    if (!UI || UI->getParent() != BB)
      continue;
    if (llvm::isa<llvm::PHINode>(UI))
      continue; // PHI uses don't constrain order within this BB
    for (size_t k = 0; k < insts.size(); ++k)
      if (insts[k] == UI && k < hi)
        hi = k;
  }

  if (lo >= hi)
    return false;

  // Pick a new index in [lo, hi) different from curIdx.
  if (hi - lo <= 1 && curIdx >= lo && curIdx < hi)
    return false;

  std::uniform_int_distribution<size_t> pickIdx(lo, hi - 1);
  size_t newIdx;
  for (int tries = 0; tries < 8; ++tries) {
    newIdx = pickIdx(rng);
    if (newIdx != curIdx)
      break;
  }
  if (newIdx == curIdx)
    return false;

  I->moveBefore(*insts[newIdx]->getParent(), insts[newIdx]->getIterator());
  return true;
}

} // namespace regatoni
