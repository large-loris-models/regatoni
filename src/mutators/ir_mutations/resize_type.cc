// src/mutators/ir_mutations/resize_type.cc
#include "src/mutators/ir_mutations/resize_type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include <vector>

namespace regatoni {

static bool isIntTarget(const llvm::BinaryOperator &BO) {
  auto *ty = BO.getType();
  if (!ty->isIntegerTy())
    return false;
  unsigned bw = ty->getIntegerBitWidth();
  return bw >= 8 && bw <= 64;
}

static bool isFPTarget(const llvm::BinaryOperator &BO) {
  auto *ty = BO.getType();
  return ty->isFloatTy() || ty->isDoubleTy();
}

bool ResizeType::canApply(const llvm::Module &M) const {
  for (const auto &F : M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I))
          if (isIntTarget(*BO) || isFPTarget(*BO))
            return true;
  return false;
}

bool ResizeType::apply(llvm::Module &M, std::mt19937 &rng) {
  std::vector<llvm::BinaryOperator *> targets;
  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I))
          if (isIntTarget(*BO) || isFPTarget(*BO))
            targets.push_back(BO);

  if (targets.empty())
    return false;

  std::uniform_int_distribution<size_t> pick(0, targets.size() - 1);
  auto *BO = targets[pick(rng)];
  auto &Ctx = BO->getContext();

  llvm::IRBuilder<> B(BO);
  llvm::Type *origTy = BO->getType();
  llvm::Value *origL = BO->getOperand(0);
  llvm::Value *origR = BO->getOperand(1);
  auto opcode = BO->getOpcode();

  llvm::Value *newInst = nullptr;

  if (origTy->isIntegerTy()) {
    unsigned bw = origTy->getIntegerBitWidth();
    // Pick a different bit-width in {8,16,32,64} that isn't the current.
    unsigned choices[] = {8, 16, 32, 64};
    std::vector<unsigned> opts;
    for (unsigned c : choices)
      if (c != bw)
        opts.push_back(c);
    std::uniform_int_distribution<size_t> pc(0, opts.size() - 1);
    unsigned nb = opts[pc(rng)];
    auto *nt = llvm::IntegerType::get(Ctx, nb);

    auto cast = [&](llvm::Value *v) -> llvm::Value * {
      if (nb > bw) {
        std::bernoulli_distribution signedExt(0.5);
        return signedExt(rng) ? B.CreateSExt(v, nt) : B.CreateZExt(v, nt);
      }
      return B.CreateTrunc(v, nt);
    };
    llvm::Value *l = cast(origL);
    llvm::Value *r = cast(origR);
    newInst = B.CreateBinOp(opcode, l, r);
    // Cast result back to original width.
    llvm::Value *back;
    if (nb > bw)
      back = B.CreateTrunc(newInst, origTy);
    else
      back = B.CreateSExt(newInst, origTy);
    BO->replaceAllUsesWith(back);
    BO->eraseFromParent();
    return true;
  }

  // FP case: float <-> double.
  llvm::Type *nt = origTy->isFloatTy()
                       ? (llvm::Type *)llvm::Type::getDoubleTy(Ctx)
                       : (llvm::Type *)llvm::Type::getFloatTy(Ctx);
  bool widen = origTy->isFloatTy();

  auto castFP = [&](llvm::Value *v) -> llvm::Value * {
    return widen ? B.CreateFPExt(v, nt) : B.CreateFPTrunc(v, nt);
  };
  llvm::Value *l = castFP(origL);
  llvm::Value *r = castFP(origR);
  newInst = B.CreateBinOp(opcode, l, r);
  llvm::Value *back = widen ? B.CreateFPTrunc(newInst, origTy)
                            : B.CreateFPExt(newInst, origTy);
  BO->replaceAllUsesWith(back);
  BO->eraseFromParent();
  return true;
}

} // namespace regatoni
