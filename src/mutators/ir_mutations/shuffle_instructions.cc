// src/mutators/ir_mutations/shuffle_instructions.cc
#include "src/mutators/ir_mutations/shuffle_instructions.h"
#include "llvm/IR/Instructions.h"
#include <vector>

namespace regatoni {

static bool isShuffleable(const llvm::Instruction &I) {
  if (I.isTerminator() || llvm::isa<llvm::PHINode>(I))
    return false;
  if (I.mayReadOrWriteMemory())
    return false;
  if (llvm::isa<llvm::CallBase>(I))
    return false;
  return true;
}

static bool usesValue(const llvm::Instruction &user, const llvm::Value *v) {
  for (const auto &U : user.operands())
    if (U.get() == v)
      return true;
  return false;
}

// Collect pairs (A, B) of adjacent shuffleable instructions in the same
// basic block, where B is immediately after A and B does not use A.
static void collectPairs(
    llvm::Module &M,
    std::vector<std::pair<llvm::Instruction *, llvm::Instruction *>> &pairs) {
  for (auto &F : M) {
    for (auto &BB : F) {
      llvm::Instruction *prev = nullptr;
      for (auto &I : BB) {
        if (!isShuffleable(I)) {
          prev = nullptr;
          continue;
        }
        if (prev && !usesValue(I, prev))
          pairs.emplace_back(prev, &I);
        prev = &I;
      }
    }
  }
}

bool ShuffleInstructions::canApply(const llvm::Module &M) const {
  // Need non-const access to iterate the same way; replicate the check
  // over const references.
  for (const auto &F : M) {
    for (const auto &BB : F) {
      const llvm::Instruction *prev = nullptr;
      for (const auto &I : BB) {
        if (!isShuffleable(I)) {
          prev = nullptr;
          continue;
        }
        if (prev && !usesValue(I, prev))
          return true;
        prev = &I;
      }
    }
  }
  return false;
}

bool ShuffleInstructions::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<std::pair<llvm::Instruction *, llvm::Instruction *>> pairs;
  collectPairs(M, pairs);
  if (pairs.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, pairs.size() - 1);
  auto [A, B] = pairs[pick(rng)];

  // Move B before A.
  B->moveBefore(*A->getParent(), A->getIterator());
  return true;
}

} // namespace regatoni
