// src/mutators/ir_mutations/narrow_then_widen.cc
#include "src/mutators/ir_mutations/narrow_then_widen.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include <vector>

namespace regatoni {

// A data binop whose result is wider than 32 bits, so truncating its operands
// to i32 is a real narrowing that creates the *W-instruction shape.
static bool isWideIntBinop(const llvm::Instruction &I) {
  auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I);
  if (!BO)
    return false;
  auto *ty = BO->getType();
  return ty->isIntegerTy() && ty->getIntegerBitWidth() > 32;
}

bool NarrowThenWiden::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (isWideIntBinop(I))
          return true;
  return false;
}

bool NarrowThenWiden::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::BinaryOperator *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (isWideIntBinop(I))
          targets.push_back(llvm::cast<llvm::BinaryOperator>(&I));
  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, targets.size() - 1);
  auto *BO = targets[pick(rng)];
  auto *origTy = BO->getType();
  auto *i32 = llvm::Type::getInt32Ty(BO->getContext());

  llvm::IRBuilder<> B(BO);
  // sext(op(trunc a, trunc b)) — plain ops, no wrap flags.
  llvm::Value *a = B.CreateTrunc(BO->getOperand(0), i32);
  llvm::Value *b = B.CreateTrunc(BO->getOperand(1), i32);
  llvm::Value *narrow = B.CreateBinOp(BO->getOpcode(), a, b);
  llvm::Value *wide = B.CreateSExt(narrow, origTy);
  BO->replaceAllUsesWith(wide);
  BO->eraseFromParent();
  return true;
}

} // namespace regatoni
